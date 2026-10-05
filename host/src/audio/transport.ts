import * as cp from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import * as net from "node:net";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const SNAU_MAGIC = 0x55414e53; // 'SNAU' in little endian

export class AudioTransport {
  private adbPath: string;
  private deviceAddress: string;
  private startPromise: Promise<void> | null = null;
  private recordingStartTime = 0;
  private recordSocket?: net.Socket;
  private recordedChunks: Buffer[] = [];
  private isUsingTcp = false;
  public tcpPort?: number;

  constructor(deviceAddress = process.env.SNOWBALL_DEVICE_ADB || "192.168.1.248:5555", adbPath?: string, tcpPort?: number) {
    this.deviceAddress = deviceAddress;
    this.adbPath = adbPath || AudioTransport.discoverAdb();
    this.tcpPort = tcpPort || (process.env.SNOWBALL_AUDIO_PORT ? parseInt(process.env.SNOWBALL_AUDIO_PORT, 10) : undefined);
  }

  public static discoverAdb(): string {
    if (process.env.ADB_PATH && fs.existsSync(process.env.ADB_PATH)) {
      return process.env.ADB_PATH;
    }
    if (process.platform === "win32") {
      const localAppData =
        process.env.LOCALAPPDATA ||
        (process.env.USERPROFILE ? path.join(process.env.USERPROFILE, "AppData", "Local") : "");
      if (localAppData) {
        const candidate = path.join(localAppData, "Temp", "Codex-MK20-ADB", "platform-tools", "adb.exe");
        if (fs.existsSync(candidate)) {
          return candidate;
        }
      }
    } else if (process.platform === "darwin") {
      const macCandidates = [
        path.join(os.homedir(), "Library", "Android", "sdk", "platform-tools", "adb"),
        "/opt/homebrew/bin/adb",
        "/usr/local/bin/adb",
      ];
      for (const c of macCandidates) {
        if (fs.existsSync(c)) return c;
      }
    } else {
      // Linux
      const linuxCandidates = [
        path.join(os.homedir(), "Android", "Sdk", "platform-tools", "adb"),
        "/usr/bin/adb",
        "/usr/local/bin/adb",
      ];
      for (const c of linuxCandidates) {
        if (fs.existsSync(c)) return c;
      }
    }
    return "adb";
  }

  private execAdb(args: string[]): Promise<{ stdout: string; stderr: string }> {
    return new Promise((resolve, reject) => {
      cp.execFile(this.adbPath, args, { windowsHide: true, timeout: 30000, maxBuffer: 4 * 1024 * 1024 }, (err, stdout, stderr) => {
        if (err) {
          return reject(err);
        }
        resolve({ stdout: stdout ? stdout.toString() : "", stderr: stderr ? stderr.toString() : "" });
      });
    });
  }

  public async ensureConnected(): Promise<void> {
    if (!this.deviceAddress || !this.deviceAddress.includes(":")) {
      return;
    }
    try {
      const { stdout } = await this.execAdb(["devices"]);
      const lines = stdout.split(/\r?\n/);
      const isConnected = lines.some((line) => line.startsWith(this.deviceAddress) && line.includes("device"));
      if (!isConnected) {
        console.log(`[AudioTransport] Connecting to ADB device ${this.deviceAddress}...`);
        await this.execAdb(["connect", this.deviceAddress]);
      }
    } catch (e: any) {
      console.warn(`[AudioTransport] ensureConnected warning:`, e.message);
    }
  }

  /**
   * Deploys the voice_rec.sh helper script to MK20 if not already present.
   */
  public async ensureDeviceHelper(): Promise<void> {
    await this.ensureConnected();
    const localHelper = path.resolve(__dirname, "../../../hardware/mk20/hud/voice_rec.sh");
    if (!fs.existsSync(localHelper)) {
      return;
    }
    try {
      const { stdout } = await this.execAdb(["-s", this.deviceAddress, "shell", "[ -x /mnt/SDCARD/voice_rec.sh ] && echo OK || echo MISSING"]);
      if (stdout && stdout.includes("OK")) {
        return;
      }
      console.log("[AudioTransport] Deploying voice_rec.sh helper to MK20...");
      await this.execAdb(["-s", this.deviceAddress, "push", localHelper, "/mnt/SDCARD/voice_rec.sh"]);
      await this.execAdb(["-s", this.deviceAddress, "shell", "chmod +x /mnt/SDCARD/voice_rec.sh"]);
    } catch (e: any) {
      console.warn("[AudioTransport] ensureDeviceHelper warning:", e.message);
    }
  }

  /**
   * Configures ALSA mixer microphone gains and single-ended input routing on MK20.
   */
  public async initMixerGains(): Promise<void> {
    await this.ensureConnected();
    const mixerCmd = "amixer sset 'MIC1 gain volume' 31; amixer sset 'ADC1 volume' 255; amixer sset 'MIC1 Input Select' 'MIC_SINGLE'; amixer sset 'MIC2 gain volume' 31; amixer sset 'ADC2 volume' 255; amixer sset 'MIC2 Input Select' 'MIC_SINGLE'; amixer sset 'MIC3 gain volume' 31; amixer sset 'ADC3 volume' 255; amixer sset 'MIC3 Input Select' 'MIC_SINGLE'";
    try {
      await this.execAdb(["-s", this.deviceAddress, "shell", mixerCmd]);
      console.log("[AudioTransport] MK20 microphone mixer gains initialized (100%).");
    } catch (err: any) {
      console.warn("[AudioTransport] Mixer initialization warning:", err.message);
    }
  }

  /**
   * Converts 3-channel 16kHz 16-bit LE PCM data into standard mono 16kHz 16-bit LE WAV
   * by extracting Channel 3 (the physical MK20 microphone).
   */
  public static pcm3chToMonoWav(raw3ch: Buffer): Buffer {
    const nSamples = Math.floor(raw3ch.length / 6);
    const dataSize = nSamples * 2;
    const wav = Buffer.alloc(44 + dataSize);

    // RIFF chunk descriptor
    wav.write("RIFF", 0);
    wav.writeUInt32LE(36 + dataSize, 4);
    wav.write("WAVE", 8);

    // fmt sub-chunk
    wav.write("fmt ", 12);
    wav.writeUInt32LE(16, 16); // Subchunk1Size (16 for PCM)
    wav.writeUInt16LE(1, 20);  // AudioFormat (1 = PCM)
    wav.writeUInt16LE(1, 22);  // NumChannels (1 = Mono)
    wav.writeUInt32LE(16000, 24); // SampleRate (16000 Hz)
    wav.writeUInt32LE(32000, 28); // ByteRate (16000 * 1 * 2)
    wav.writeUInt16LE(2, 32);  // BlockAlign (1 * 2)
    wav.writeUInt16LE(16, 34); // BitsPerSample (16 bits)

    // data sub-chunk
    wav.write("data", 36);
    wav.writeUInt32LE(dataSize, 40);

    // Extract Channel 3 (bytes 4 and 5 of each 6-byte frame)
    let dst = 44;
    for (let i = 0; i < nSamples; i++) {
      const src = i * 6 + 4;
      wav[dst] = raw3ch[src];
      wav[dst + 1] = raw3ch[src + 1];
      dst += 2;
    }

    return wav;
  }

  /**
   * Converts 1-channel 16kHz 16-bit LE PCM data into standard mono 16kHz 16-bit LE WAV.
   */
  public static monoPcmToWav(pcm: Buffer, sampleRate = 16000): Buffer {
    const dataSize = pcm.length;
    const wav = Buffer.alloc(44 + dataSize);

    // RIFF chunk descriptor
    wav.write("RIFF", 0);
    wav.writeUInt32LE(36 + dataSize, 4);
    wav.write("WAVE", 8);

    // fmt sub-chunk
    wav.write("fmt ", 12);
    wav.writeUInt32LE(16, 16); // Subchunk1Size (16 for PCM)
    wav.writeUInt16LE(1, 20);  // AudioFormat (1 = PCM)
    wav.writeUInt16LE(1, 22);  // NumChannels (1 = Mono)
    wav.writeUInt32LE(sampleRate, 24); // SampleRate
    wav.writeUInt32LE(sampleRate * 2, 28); // ByteRate (sampleRate * 1 * 2)
    wav.writeUInt16LE(2, 32);  // BlockAlign (1 * 2)
    wav.writeUInt16LE(16, 34); // BitsPerSample (16 bits)

    // data sub-chunk
    wav.write("data", 36);
    wav.writeUInt32LE(dataSize, 40);
    pcm.copy(wav, 44);

    return wav;
  }

  /**
   * Pulls recorded 3-channel audio from MK20, extracts Channel 3 (MIC3),
   * and saves a clean 16kHz mono WAV file for local transcription.
   */
  public async pullDeviceWav(remotePath = "/tmp/snowball_voice.pcm"): Promise<string> {
    if (this.isUsingTcp) {
      const pcm = Buffer.concat(this.recordedChunks);
      this.recordedChunks = [];
      this.isUsingTcp = false;

      if (pcm.length < 960) {
        throw new Error(`Audio buffer too short: ${pcm.length} bytes`);
      }
      const localWav = path.join(os.tmpdir(), `mk20_voice_${Date.now()}.wav`);
      const wavBuf = AudioTransport.monoPcmToWav(pcm, 16000);
      fs.writeFileSync(localWav, wavBuf);
      return localWav;
    }

    if (remotePath !== "/tmp/snowball_voice.pcm") throw new Error("Unsupported recording scratch path");
    await this.ensureConnected();
    const checkRes = await this.execAdb([
      "-s",
      this.deviceAddress,
      "shell",
      `[ -s '${remotePath}' ] && ls -l '${remotePath}' || echo MISSING`
    ]).catch(() => ({ stdout: "MISSING", stderr: "" }));

    if ((checkRes.stdout || "").includes("MISSING")) {
      throw new Error("No audio was recorded on MK20. The microphone did not produce audio data.");
    }

    const tempPcm = path.join(os.tmpdir(), `mk20_raw_${Date.now()}.pcm`);
    const localWav = path.join(os.tmpdir(), `mk20_voice_${Date.now()}.wav`);

    try {
      await this.execAdb(["-s", this.deviceAddress, "pull", remotePath, tempPcm]);
    } catch (err: any) {
      console.warn("[AudioTransport] ADB pull failed:", err.message);
      throw err;
    }

    if (!fs.existsSync(tempPcm)) {
      throw new Error(`Pulled file missing: ${tempPcm}`);
    }

    try {
      const rawBytes = fs.readFileSync(tempPcm);
      fs.rmSync(tempPcm, { force: true });

      if (rawBytes.length < 960) {
        throw new Error(`Audio buffer too short: ${rawBytes.length} bytes`);
      }

      const monoWav = AudioTransport.pcm3chToMonoWav(rawBytes);
      fs.writeFileSync(localWav, monoWav);

      // Clean up MK20 remote temp files asynchronously
      this.execAdb(["-s", this.deviceAddress, "shell", `rm -f '${remotePath}' /tmp/snowball_arecord.pid`]).catch(() => {});

      return localWav;
    } catch (convErr) {
      throw convErr;
    }
  }

  private recorderProcess?: cp.ChildProcess;

  /**
   * Triggers background ALSA recording on the physical MK20 device.
   * Priority 1: Native TCP streaming from mk20-audio daemon (MIC3, 16kHz mono).
   * Priority 2: Fallback to ADB arecord if daemon is unavailable.
   */
  public async startDeviceRecording(remotePath = "/tmp/snowball_voice.pcm"): Promise<void> {
    this.recordingStartTime = Date.now();

    // 1. Try Native TCP Audio Daemon (7702) first: zero ADB, streaming directly to host memory
    const rawIp = this.deviceAddress.split(":")[0];
    const targetHost =
      (rawIp === "127.0.0.1" || rawIp === "localhost") && !this.tcpPort
        ? (process.env.MK20_IP || process.env.SNOWBALL_MK20_IP || "192.168.1.248")
        : (process.env.MK20_IP || process.env.SNOWBALL_MK20_IP || rawIp);
    const targetPort =
      this.tcpPort ||
      (this.deviceAddress.includes(":") && !this.deviceAddress.endsWith(":5555")
        ? parseInt(this.deviceAddress.split(":")[1], 10)
        : parseInt(process.env.SNOWBALL_AUDIO_PORT || "7702", 10));

    try {
      await new Promise<void>((resolve, reject) => {
        let connected = false;
        let socket: net.Socket;
        const timer = setTimeout(() => {
          if (!connected) {
            try { socket?.destroy(); } catch {}
            reject(new Error("Connection to mk20-audio TCP port timed out"));
          }
        }, 1500);

        socket = net.createConnection({ host: targetHost, port: targetPort }, () => {
          connected = true;
          clearTimeout(timer);
          this.isUsingTcp = true;
          this.recordedChunks = [];
          this.recordSocket = socket;

          const hdr = Buffer.alloc(16);
          hdr.writeUInt32LE(SNAU_MAGIC, 0); // SNAU
          hdr.writeUInt8(2, 4);             // MODE_RECORD
          hdr.writeUInt8(1, 5);             // 1 ch mono
          hdr.writeUInt8(100, 6);           // Volume 100
          hdr.writeUInt8(0, 7);             // Not muted
          hdr.writeUInt32LE(16000, 8);      // 16000 Hz
          hdr.writeUInt32LE(0xFFFFFFFF, 12);// Streaming
          socket.write(hdr);
          resolve();
        });

        socket.on("data", (chunk) => {
          this.recordedChunks.push(chunk);
        });

        socket.on("error", (err) => {
          clearTimeout(timer);
          if (!connected) {
            reject(err);
          }
        });
      });
      return;
    } catch (tcpErr: any) {
      this.isUsingTcp = false;
      this.recordSocket = undefined;
      console.warn(`[AudioTransport] Native TCP daemon unreachable (${tcpErr.message}), falling back to ADB arecord...`);
    }

    // 2. Legacy ADB Fallback
    if (remotePath !== "/tmp/snowball_voice.pcm") throw new Error("Unsupported recording scratch path");
    await this.ensureConnected();
    // Clean up any old files first
    await this.execAdb([
      "-s",
      this.deviceAddress,
      "shell",
      `if [ -s /tmp/snowball_arecord.pid ]; then p=$(cat /tmp/snowball_arecord.pid); case $p in ''|*[!0-9]*) p=0;; esac; if [ "$p" != 0 ] && [ -r /proc/$p/cmdline ] && tr '\\000' ' ' < /proc/$p/cmdline | grep -q 'arecord.*snowball_voice.pcm'; then kill -2 "$p"; fi; fi; rm -f '${remotePath}' /tmp/snowball_arecord.pid`
    ]);

    const shellArgs = [
      "-s",
      this.deviceAddress,
      "shell",
      `arecord -q -D hw:0,0 -f S16_LE -r 16000 -c 3 -t raw -d 600 -F 20000 -B 80000 --process-id-file=/tmp/snowball_arecord.pid '${remotePath}'`
    ];

    this.recorderProcess = cp.spawn(this.adbPath, shellArgs, { windowsHide: true });
    let recorderError: Error | undefined;
    this.recorderProcess.on("error", (err) => { recorderError = err; });
    const deadline = Date.now() + 3000;
    while (Date.now() < deadline) {
      if (recorderError) throw recorderError;
      if (this.recorderProcess.exitCode !== null) throw new Error("MK20 recorder exited before capture started");
      const result = await this.execAdb(["-s", this.deviceAddress, "shell",
        "if [ -s /tmp/snowball_arecord.pid ] && [ -s /tmp/snowball_voice.pcm ]; then p=$(cat /tmp/snowball_arecord.pid); case $p in ''|*[!0-9]*) exit 1;; esac; kill -0 $p 2>/dev/null && echo snowball_recording_ready; fi"]);
      if (result.stdout.trim() === "snowball_recording_ready") return;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    await this.cancelDeviceRecording();
    throw new Error("MK20 microphone did not produce audio before the startup deadline");
  }

  /**
   * Stops background recording on the physical MK20 device safely.
   */
  public async stopDeviceRecording(): Promise<void> {
    const elapsed = Date.now() - this.recordingStartTime;
    if (elapsed < 300) {
      await new Promise((r) => setTimeout(r, 300 - elapsed));
    }

    if (this.isUsingTcp && this.recordSocket) {
      const socket = this.recordSocket;
      this.recordSocket = undefined;
      await new Promise<void>((resolve) => {
        const timer = setTimeout(() => {
          try { socket.destroy(); } catch {}
          resolve();
        }, 1500);
        socket.on("close", () => {
          clearTimeout(timer);
          resolve();
        });
        try {
          socket.write(Buffer.from([0]));
          setTimeout(() => { try { socket.end(); } catch {} }, 50);
        } catch {
          clearTimeout(timer);
          try { socket.destroy(); } catch {}
          resolve();
        }
      });
      return;
    }

    // Signal MK20 arecord to stop cleanly via ADB
    const stopCmd = "if [ -s /tmp/snowball_arecord.pid ]; then p=$(cat /tmp/snowball_arecord.pid); case $p in ''|*[!0-9]*) exit 0;; esac; if [ -r /proc/$p/cmdline ] && tr '\\000' ' ' < /proc/$p/cmdline | grep -q 'arecord.*snowball_voice.pcm'; then kill -2 $p; fi; fi";
    await this.execAdb(["-s", this.deviceAddress, "shell", stopCmd]).catch(() => {});

    // Wait for local adb child process to exit
    if (this.recorderProcess && !this.recorderProcess.killed && this.recorderProcess.exitCode === null) {
      await new Promise<void>((resolve) => {
        const timer = setTimeout(() => {
          try { this.recorderProcess?.kill(); } catch {}
          resolve();
        }, 3000);
        this.recorderProcess?.on("exit", () => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
    this.recorderProcess = undefined;
  }

  /**
   * Cancels in-flight recording and cleans up temporary resources.
   */
  public async cancelDeviceRecording(remotePath = "/tmp/snowball_voice.pcm"): Promise<void> {
    if (this.isUsingTcp) {
      if (this.recordSocket) {
        try { this.recordSocket.destroy(); } catch {}
        this.recordSocket = undefined;
      }
      this.recordedChunks = [];
      this.isUsingTcp = false;
      return;
    }

    if (remotePath !== "/tmp/snowball_voice.pcm") throw new Error("Unsupported recording scratch path");
    await this.stopDeviceRecording();
    await this.execAdb(["-s", this.deviceAddress, "shell", `rm -f '${remotePath}' /tmp/snowball_arecord.pid`]).catch(() => {});
  }
}

