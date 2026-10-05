import { spawn, execFile, type ChildProcess } from "node:child_process";
import { EventEmitter } from "node:events";
import * as fs from "node:fs";
import { AudioTransport } from "./transport.js";

export class AudioPlayer extends EventEmitter {
  private activeProc?: ChildProcess;
  private activeDeviceProc?: ChildProcess;
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

    this._isPlaying = true;
    this.emit("play", { wavPath, target: "host" });

    return new Promise<void>((resolve) => {
      let proc: ChildProcess;

      if (process.platform === "win32") {
        // Use PowerShell SoundPlayer for instant zero-dependency synchronous playback
        const psScript = `
          $player = New-Object System.Media.SoundPlayer('${wavPath.replace(/'/g, "''")}');
          $player.PlaySync();
          $player.Dispose();
        `;
        const cmd = psScript.split(/\r?\n/).map(s => s.trim()).filter(Boolean).join(" ");
        proc = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command", cmd], {
          windowsHide: true,
          stdio: "ignore",
        });
      } else if (process.platform === "darwin") {
        proc = spawn("afplay", [wavPath], { stdio: "ignore" });
      } else {
        // Linux: try aplay or paplay
        proc = spawn("aplay", [wavPath], { stdio: "ignore" });
      }

      this.activeProc = proc;

      proc.on("exit", () => {
        this._isPlaying = false;
        this.activeProc = undefined;
        this.emit("ended", { wavPath, target: "host" });
        resolve();
      });

      proc.on("error", (err) => {
        this._isPlaying = false;
        this.activeProc = undefined;
        this.emit("error", err);
        resolve();
      });
    });
  }

  /**
   * Plays a WAV audio file directly on the MK20 hardware onboard speaker.
   */
  public async playOnDevice(wavPath: string, volume = 1.0): Promise<void> {
    if (!fs.existsSync(wavPath)) {
      throw new Error(`Audio file does not exist: ${wavPath}`);
    }

    await this.stop();
    this._isPlaying = true;
    this.emit("play", { wavPath, target: "device" });

    const remoteWav = `/tmp/tts_chunk_${Date.now() % 10000}.wav`;

    return new Promise<void>((resolve) => {
      // 1. Push audio to MK20 RAM filesystem (/tmp)
      execFile(this.adbPath, ["-s", this.deviceAddress, "push", wavPath, remoteWav], { windowsHide: true, timeout: 5000 }, (pushErr) => {
        if (pushErr) {
          console.warn("[AudioPlayer] Failed to push audio to MK20:", pushErr.message);
          this._isPlaying = false;
          return resolve();
        }

        // 2. Play via ALSA aplay on MK20
        const proc = spawn(this.adbPath, ["-s", this.deviceAddress, "shell", "aplay", "-q", remoteWav], {
          windowsHide: true,
          stdio: "ignore",
        });

        this.activeDeviceProc = proc;

        const cleanup = () => {
          this._isPlaying = false;
          this.activeDeviceProc = undefined;
          this.emit("ended", { wavPath, target: "device" });
          // Async remove temp file
          execFile(this.adbPath, ["-s", this.deviceAddress, "shell", "rm", "-f", remoteWav], { windowsHide: true }, () => {});
          resolve();
        };

        proc.on("exit", cleanup);
        proc.on("error", cleanup);
      });
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

