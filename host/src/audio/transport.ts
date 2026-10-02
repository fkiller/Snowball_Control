import * as cp from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export class AudioTransport {
  private adbPath: string;
  private deviceAddress: string;
  private startPromise: Promise<void> | null = null;
  private recordingStartTime = 0;

  constructor(deviceAddress = process.env.SNOWBALL_DEVICE_ADB || "192.168.1.248:5555", adbPath?: string) {
    this.deviceAddress = deviceAddress;
    this.adbPath = adbPath || AudioTransport.discoverAdb();
  }

  public static discoverAdb(): string {
    const localAppData =
      process.env.LOCALAPPDATA ||
      (process.env.USERPROFILE ? path.join(process.env.USERPROFILE, "AppData", "Local") : "");
    if (localAppData) {
      const candidate = path.join(localAppData, "Temp", "Codex-MK20-ADB", "platform-tools", "adb.exe");
      if (fs.existsSync(candidate)) {
        return candidate;
      }
    }
    return "adb";
  }

  private execAdb(args: string[]): Promise<{ stdout: string; stderr: string }> {
    return new Promise((resolve, reject) => {
      cp.execFile(this.adbPath, args, { windowsHide: true }, (err, stdout, stderr) => {
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
   * Pulls recorded 3-channel audio from MK20, extracts Channel 3 (MIC3),
   * and saves a clean 16kHz mono WAV file for local transcription.
   */
  public async pullDeviceWav(remotePath = "/tmp/snowball_voice.pcm"): Promise<string> {
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
   * Triggers background 3-channel 16kHz ALSA recording on the physical MK20 device.
   * MIC3 carries the physical microphone. Stops automatically after 600s if not finished.
   */
  public async startDeviceRecording(remotePath = "/tmp/snowball_voice.pcm"): Promise<void> {
    await this.ensureConnected();
    this.recordingStartTime = Date.now();
    // Clean up any old files first
    await this.execAdb([
      "-s",
      this.deviceAddress,
      "shell",
      `killall -9 arecord 2>/dev/null; rm -f '${remotePath}' /tmp/snowball_arecord.pid`
    ]).catch(() => {});

    const shellArgs = [
      "-s",
      this.deviceAddress,
      "shell",
      `arecord -q -D hw:0,0 -f S16_LE -r 16000 -c 3 -t raw -d 600 -F 20000 -B 80000 --process-id-file=/tmp/snowball_arecord.pid '${remotePath}'`
    ];

    this.recorderProcess = cp.spawn(this.adbPath, shellArgs, { windowsHide: true });
    this.recorderProcess.on("error", (err) => {
      console.warn("[AudioTransport] arecord spawn error:", err.message);
    });

    // Wait 150ms for ALSA device initialization
    await new Promise((r) => setTimeout(r, 150));
  }

  /**
   * Stops background recording on the physical MK20 device safely via SIGINT.
   */
  public async stopDeviceRecording(): Promise<void> {
    const elapsed = Date.now() - this.recordingStartTime;
    if (elapsed < 300) {
      await new Promise((r) => setTimeout(r, 300 - elapsed));
    }

    // Signal MK20 arecord to stop cleanly
    const stopCmd = "if [ -s /tmp/snowball_arecord.pid ]; then p=$(cat /tmp/snowball_arecord.pid); case $p in ''|*[!0-9]*) exit 0;; esac; if [ -r /proc/$p/cmdline ]; then kill -2 $p; fi; fi";
    await this.execAdb(["-s", this.deviceAddress, "shell", stopCmd]).catch(() => {});

    // Wait for local adb child process to exit
    if (this.recorderProcess && !this.recorderProcess.killed) {
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
      this.recorderProcess = undefined;
    }
  }

  /**
   * Cancels in-flight recording and removes remote scratch files.
   */
  public async cancelDeviceRecording(remotePath = "/tmp/snowball_voice.pcm"): Promise<void> {
    await this.stopDeviceRecording();
    await this.execAdb(["-s", this.deviceAddress, "shell", `rm -f '${remotePath}' /tmp/snowball_arecord.pid`]).catch(() => {});
  }
}

