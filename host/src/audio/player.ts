import { spawn, execFile, type ChildProcess } from "node:child_process";
import { EventEmitter } from "node:events";
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import * as net from "node:net";
import { AudioTransport } from "./transport.js";

const SNAU_MAGIC = 0x55414e53; // 'SNAU' in little endian

export class AudioPlayer extends EventEmitter {
  private activeProc?: ChildProcess;
  private activeDeviceProc?: ChildProcess;
  private activeDeviceSocket?: net.Socket;
  private _isPlaying = false;
  private adbPath: string;
  private deviceAddress: string;

  constructor(deviceAddress = process.env.SNOWBALL_DEVICE_ADB || "127.0.0.1:15555", adbPath?: string) {
    super();
    this.deviceAddress = deviceAddress;
    this.adbPath = adbPath || AudioTransport.discoverAdb();
  }

  get isPlaying(): boolean {
    return this._isPlaying;
  }

  /**
   * Plays a WAV audio file on the host system speakers (PC / Mac).
   * Resolves when playback completes, or immediately returns if stopped.
   */
  public async play(wavPath: string, volume = 1.0): Promise<void> {
    if (!fs.existsSync(wavPath)) {
      throw new Error(`Audio file does not exist: ${wavPath}`);
    }

    // Stop any in-flight playback first
    await this.stop();

    const volClamped = Math.max(0, Math.min(1.0, volume));
    if (volClamped <= 0.001) {
      return;
    }

    this._isPlaying = true;
    this.emit("play", { wavPath, target: "host" });

    let playbackWav = wavPath;
    let tempScaledWav: string | undefined;

    // Apply software PCM volume scaling for host output if volume < 0.99
    if (volClamped < 0.99) {
      try {
        const rawWav = fs.readFileSync(wavPath);
        if (rawWav.length > 44 && rawWav.toString("utf8", 0, 4) === "RIFF") {
          let dataOffset = 44;
          const dataIdx = rawWav.indexOf("data");
          if (dataIdx >= 0 && dataIdx + 8 <= rawWav.length) {
            dataOffset = dataIdx + 8;
          }
          const scaledWav = Buffer.from(rawWav);
          const numSamples = Math.floor((rawWav.length - dataOffset) / 2);
          for (let i = 0; i < numSamples; i++) {
            const sample = rawWav.readInt16LE(dataOffset + i * 2);
            const scaled = Math.max(-32768, Math.min(32767, Math.round(sample * volClamped)));
            scaledWav.writeInt16LE(scaled, dataOffset + i * 2);
          }
          tempScaledWav = path.join(os.tmpdir(), `snowball_host_vol_${Date.now()}_${Math.random().toString(36).slice(2, 6)}.wav`);
          fs.writeFileSync(tempScaledWav, scaledWav);
          playbackWav = tempScaledWav;
        }
      } catch (err: any) {
        console.warn("[AudioPlayer] Host PCM volume scaling error:", err.message);
      }
    }

    return new Promise<void>((resolve) => {
      let proc: ChildProcess;

      if (process.platform === "win32") {
        // Use PowerShell SoundPlayer for instant zero-dependency synchronous playback
        const psScript = `
          $player = New-Object System.Media.SoundPlayer('${playbackWav.replace(/'/g, "''")}');
          $player.PlaySync();
          $player.Dispose();
        `;
        const cmd = psScript.split(/\r?\n/).map(s => s.trim()).filter(Boolean).join(" ");
        proc = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command", cmd], {
          windowsHide: true,
          stdio: "ignore",
        });
      } else if (process.platform === "darwin") {
        proc = spawn("afplay", [playbackWav], { stdio: "ignore" });
      } else {
        // Linux: try aplay or paplay
        proc = spawn("aplay", [playbackWav], { stdio: "ignore" });
      }

      this.activeProc = proc;

      const cleanup = () => {
        if (tempScaledWav) {
          try { fs.rmSync(tempScaledWav, { force: true }); } catch {}
          tempScaledWav = undefined;
        }
      };

      proc.on("exit", () => {
        cleanup();
        this._isPlaying = false;
        this.activeProc = undefined;
        this.emit("ended", { wavPath, target: "host" });
        resolve();
      });

      proc.on("error", (err) => {
        cleanup();
        this._isPlaying = false;
        this.activeProc = undefined;
        this.emit("error", err);
        resolve();
      });
    });
  }

  /**
   * Plays a WAV audio file directly on the MK20 hardware onboard speaker.
   * Priority:
   * 1. Native TCP audio daemon (SNAU protocol on port 7702) with hardware/software volume scaling
   * 2. Fallback to ADB ALSA aplay if daemon is temporarily unreachable
   */
  public async playOnDevice(wavPath: string, volume = 1.0): Promise<void> {
    if (!fs.existsSync(wavPath)) {
      throw new Error(`Audio file does not exist: ${wavPath}`);
    }

    await this.stop();
    this._isPlaying = true;
    this.emit("play", { wavPath, target: "device" });

    const volInt = Math.max(0, Math.min(100, Math.round(volume * 100)));
    const isMuted = volInt === 0 ? 1 : 0;

    // Parse WAV metadata and PCM stream
    const wavBuf = fs.readFileSync(wavPath);
    const sampleRate = wavBuf.length >= 28 ? wavBuf.readUInt32LE(24) : 24000;
    const channels = wavBuf.length >= 24 ? wavBuf.readUInt16LE(22) : 1;
    let pcmOffset = 44;
    const dataIdx = wavBuf.indexOf("data");
    if (dataIdx >= 0 && dataIdx + 8 <= wavBuf.length) {
      pcmOffset = dataIdx + 8;
    }
    const pcmBuf = wavBuf.subarray(pcmOffset);

    // Resolve target MK20 IP (LAN IP direct connect)
    const rawIp = this.deviceAddress.split(":")[0];
    const targetHost =
      rawIp === "127.0.0.1" || rawIp === "localhost"
        ? (process.env.MK20_IP || process.env.SNOWBALL_MK20_IP || "192.168.1.248")
        : rawIp;
    const targetPort = 7702;

    return new Promise<void>((resolve) => {
      let isFallback = false;

      // 1. Try Native TCP Audio Daemon (7702) first: zero ADB, zero disk I/O
      const socket = net.createConnection({ host: targetHost, port: targetPort }, () => {
        const hdr = Buffer.alloc(16);
        hdr.writeUInt32LE(SNAU_MAGIC, 0); // SNAU
        hdr.writeUInt8(1, 4);             // MODE_PLAY
        hdr.writeUInt8(channels, 5);      // Channels
        hdr.writeUInt8(volInt, 6);        // Volume 0-100
        hdr.writeUInt8(isMuted, 7);       // Muted flag
        hdr.writeUInt32LE(sampleRate, 8); // Sample rate
        hdr.writeUInt32LE(pcmBuf.length, 12); // Data length
        socket.write(hdr);
        socket.write(pcmBuf, () => {
          socket.end();
        });
      });

      this.activeDeviceSocket = socket;

      socket.on("close", () => {
        if (!isFallback) {
          this._isPlaying = false;
          this.activeDeviceSocket = undefined;
          this.emit("ended", { wavPath, target: "device" });
          resolve();
        }
      });

      socket.on("error", (tcpErr) => {
        if (!isFallback) {
          isFallback = true;
          this.activeDeviceSocket = undefined;
          console.warn("[AudioPlayer] Native TCP audio daemon (7702) unreachable:", tcpErr.message, "-> Executing ADB fallback");
          this.fallbackAdbPlay(wavPath, resolve);
        }
      });
    });
  }

  /**
   * Fallback ADB audio playback method
   */
  private fallbackAdbPlay(wavPath: string, resolve: () => void): void {
    const remoteWav = `/tmp/tts_chunk_${Date.now() % 10000}.wav`;
    execFile(this.adbPath, ["-s", this.deviceAddress, "push", wavPath, remoteWav], { windowsHide: true, timeout: 5000 }, (pushErr) => {
      if (pushErr) {
        console.warn("[AudioPlayer] Failed to push audio to MK20 via ADB:", pushErr.message);
        this._isPlaying = false;
        return resolve();
      }

      const proc = spawn(this.adbPath, ["-s", this.deviceAddress, "shell", "aplay", "-q", remoteWav], {
        windowsHide: true,
        stdio: "ignore",
      });

      this.activeDeviceProc = proc;

      const cleanup = () => {
        this._isPlaying = false;
        this.activeDeviceProc = undefined;
        this.emit("ended", { wavPath, target: "device" });
        execFile(this.adbPath, ["-s", this.deviceAddress, "shell", "rm", "-f", remoteWav], { windowsHide: true }, () => {});
        resolve();
      };

      proc.on("exit", cleanup);
      proc.on("error", cleanup);
    });
  }

  /**
   * Instantly stops any active audio playback on both host and MK20 hardware.
   */
  public async stop(): Promise<void> {
    if (this.activeProc) {
      try {
        if (process.platform === "win32" && this.activeProc.pid) {
          spawn("taskkill", ["/F", "/T", "/PID", String(this.activeProc.pid)], { windowsHide: true });
        } else {
          this.activeProc.kill("SIGTERM");
        }
      } catch {}
      this.activeProc = undefined;
    }

    if (this.activeDeviceSocket) {
      try {
        this.activeDeviceSocket.destroy();
      } catch {}
      this.activeDeviceSocket = undefined;
    }

    if (this.activeDeviceProc) {
      try {
        this.activeDeviceProc.kill("SIGKILL");
      } catch {}
      this.activeDeviceProc = undefined;
    }

    // Silence any active ALSA aplay process on MK20 hardware
    try {
      execFile(this.adbPath, ["-s", this.deviceAddress, "shell", "killall", "-9", "aplay"], { windowsHide: true, timeout: 1500 }, () => {});
    } catch {}

    this._isPlaying = false;
    this.emit("stopped");
  }
}
