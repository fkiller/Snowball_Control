import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { createInterface } from "node:readline";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import type { NativeVoiceProvider } from "./provider.js";
import { AudioTransport } from "./transport.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export class LocalWhisperProvider implements NativeVoiceProvider {
  private child?: ChildProcessWithoutNullStreams;
  private workerReady?: Promise<void>;
  private seq = 0;
  private pending = new Map<number, { resolve(value: any): void; reject(error: Error): void; timer: NodeJS.Timeout }>();
  private audioTransport: Pick<AudioTransport, "startDeviceRecording" | "stopDeviceRecording" | "cancelDeviceRecording" | "pullDeviceWav">;
  private currentCaptureId?: string;
  private captureGeneration=0;
  private activeRecording = false;

  constructor(
    private model = process.env.SNOWBALL_WHISPER_MODEL || "large-v3-turbo",
    private deviceAddress = process.env.SNOWBALL_MK20_ADDRESS || "",
    private modelDir = process.env.SNOWBALL_MODELS_DIR,
    private computeDevice = process.env.SNOWBALL_WHISPER_DEVICE || "auto",
    captureTransport?: Pick<AudioTransport, "startDeviceRecording" | "stopDeviceRecording" | "cancelDeviceRecording" | "pullDeviceWav">
  ) {
    this.audioTransport = captureTransport || new AudioTransport(this.deviceAddress);
  }

  public static resolvePythonRuntime(): { exec: string; workerPy: string } {
    const workerPy = LocalWhisperProvider.findWorkerPy();
    if (process.env.PYTHON_BIN && fs.existsSync(process.env.PYTHON_BIN)) {
      return { exec: process.env.PYTHON_BIN, workerPy };
    }

    const venvPaths = [
      path.resolve(__dirname, "../../.venv-whisper"),
      path.resolve(__dirname, "../../.venv"),
      ...(process.env.SNOWBALL_CONTROL_ROOT ? [
        path.resolve(process.env.SNOWBALL_CONTROL_ROOT, "host/.venv-whisper"),
        path.resolve(process.env.SNOWBALL_CONTROL_ROOT, ".venv-whisper"),
      ] : []),
      "E:/developments/projects/Snowball_Control/host/.venv-whisper",
      "E:/developments/projects/Snowball_Control/.venv-whisper",
      path.resolve(process.cwd(), ".venv-whisper"),
      path.resolve(process.cwd(), ".venv"),
      path.resolve(process.cwd(), "host/.venv-whisper"),
      path.resolve(process.cwd(), "host/.venv"),
    ];

    for (const venv of venvPaths) {
      const winPy = path.join(venv, "Scripts", "python.exe");
      const unixPy = path.join(venv, "bin", "python");
      const unixPy3 = path.join(venv, "bin", "python3");
      if (process.platform === "win32" && fs.existsSync(winPy)) {
        return { exec: winPy, workerPy };
      }
      if (fs.existsSync(unixPy)) {
        return { exec: unixPy, workerPy };
      }
      if (fs.existsSync(unixPy3)) {
        return { exec: unixPy3, workerPy };
      }
    }

    const fallbackExec = process.platform === "win32" ? "python" : "python3";
    return { exec: fallbackExec, workerPy };
  }

  private static findWorkerPy(): string {
    const candidates = [
      path.resolve(__dirname, "whisper_worker.py"),
      path.resolve(__dirname, "../../src/audio/whisper_worker.py"),
      path.resolve(process.cwd(), "host/src/audio/whisper_worker.py"),
      path.resolve(process.cwd(), "src/audio/whisper_worker.py"),
      path.resolve(process.cwd(), "../Snowball_Control/host/src/audio/whisper_worker.py"),
      path.resolve(process.cwd(), "../../Snowball_Control/host/src/audio/whisper_worker.py"),
    ];
    return candidates.find((c) => fs.existsSync(c)) || candidates[0];
  }

  /**
   * Ensures python STT runtime dependencies (faster-whisper, nvidia CUDA DLLs) are verified and installed.
   */
  public static async ensureDependencies(onProgress?: (msg: string) => void): Promise<{ ok: boolean; status: any }> {
    const { exec } = LocalWhisperProvider.resolvePythonRuntime();
    const candidateScripts = [
      path.resolve(__dirname, "../../../scripts/ensure_stt_runtime.py"),
      path.resolve(process.cwd(), "scripts/ensure_stt_runtime.py"),
      path.resolve(process.cwd(), "../Snowball_Middleware/scripts/ensure_stt_runtime.py"),
      path.resolve(process.cwd(), "../Snowball_Control/scripts/ensure_stt_runtime.py"),
    ];
    const script = candidateScripts.find((s) => fs.existsSync(s));
    if (!script) {
      return { ok: false, status: { error: "STT runtime verification script was not found" } };
    }

    // 1. Check current status
    const checkRes = await new Promise<{ ok: boolean; data?: any }>((resolve) => {
      const child = spawn(exec, [script, "--check"], { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
      let out = "";
      child.stdout.on("data", (d) => (out += d.toString()));
      child.on("exit", (code) => {
        try {
          resolve({ ok: code === 0, data: JSON.parse(out) });
        } catch {
          resolve({ ok: false });
        }
      });
      child.on("error", () => resolve({ ok: false }));
    });

    if (checkRes.ok && checkRes.data) {
      const st = checkRes.data;
      const needsInstall = !st.faster_whisper || (st.nvidia_gpu && !st.cublas_ready);
      if (!needsInstall) {
        return { ok: true, status: st };
      }

      if (onProgress) {
        onProgress(`[WhisperProvider] Installing STT accelerator dependencies for ${st.gpu_name || "CPU"}...`);
      }

      // 2. Perform installation
      return new Promise((resolve) => {
        const child = spawn(exec, [script, "--install", "--python", exec], { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
        let errOut = "";
        child.stderr.on("data", (d) => {
          const t = d.toString();
          errOut += t;
          if (onProgress) onProgress(t.trim());
        });
        child.on("exit", (code) => {
          resolve({ ok: code === 0, status: { installed: code === 0, error: code !== 0 ? errOut : undefined } });
        });
        child.on("error", (err) => {
          resolve({ ok: false, status: { error: err.message } });
        });
      });
    }

    return { ok: false, status: { error: "STT runtime verification failed" } };
  }

  /**
   * Checks if a whisper model is already downloaded locally in the cache directory.
   */
  public static async isModelDownloaded(model: string, modelDir?: string): Promise<boolean> {
    const { exec, workerPy } = LocalWhisperProvider.resolvePythonRuntime();
    const args = [workerPy, "--check-only", "--model", model];
    if (modelDir) args.push("--model-dir", modelDir);

    return new Promise<boolean>((resolve) => {
      const child = spawn(exec, args, { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
      child.on("exit", (code) => {
        resolve(code === 0);
      });
      child.on("error", () => {
        resolve(false);
      });
    });
  }

  /**
   * Pre-downloads or verifies a Whisper model before resident worker boot.
   */
  public static async ensureModelDownloaded(
    model: string,
    modelDir?: string,
    onProgress?: (msg: string) => void
  ): Promise<{ ok: boolean; path?: string; status: string; error?: string }> {
    const { exec, workerPy } = LocalWhisperProvider.resolvePythonRuntime();
    const args = [workerPy, "--download-only", "--model", model];
    if (modelDir) args.push("--model-dir", modelDir);

    if (onProgress) {
      onProgress(`[WhisperProvider] Verifying model '${model}'...`);
    }

    return new Promise((resolve) => {
      const child = spawn(exec, args, { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
      let stdout = "";
      let stderr = "";

      child.stdout.on("data", (d) => {
        stdout += d.toString();
      });
      child.stderr.on("data", (d) => {
        const text = d.toString();
        stderr += text;
        if (onProgress) {
          onProgress(text.trim());
        }
      });

      child.on("exit", (code) => {
        try {
          const lines = stdout.trim().split("\n");
          const lastJson = JSON.parse(lines[lines.length - 1]);
          if (code === 0) {
            resolve({ ok: true, status: lastJson.status, path: lastJson.path });
          } else {
            resolve({ ok: false, status: "error", error: lastJson.error || stderr || `Exit code ${code}` });
          }
        } catch {
          resolve({ ok: false, status: "error", error: stderr || `Model download returned no valid acknowledgement (exit ${code})` });
        }
      });

      child.on("error", (err) => {
        resolve({ ok: false, status: "error", error: err.message });
      });
    });
  }

  private resolvePython(): { exec: string; args: string[] } {
    const { exec, workerPy } = LocalWhisperProvider.resolvePythonRuntime();
    const args = [workerPy, "--worker", "--model", this.model];
    if (this.computeDevice) {
      args.push("--device", this.computeDevice);
    }
    if (this.modelDir) {
      args.push("--model-dir", this.modelDir);
    }
    return { exec, args };
  }

  private ensureWorkerStarted(): Promise<void> {
    if (this.workerReady) return this.workerReady;

    const { exec, args } = this.resolvePython();
    console.log(`[LocalWhisper] Spawning resident worker: ${exec} ${args.join(" ")}`);

    this.workerReady = new Promise<void>((resolve, reject) => {
      let resolved = false;
      const child = spawn(exec, args, {
        stdio: "pipe",
        windowsHide: true,
      });

      this.child = child;
      child.stderr.on("data", (data) => {
        process.stderr.write(data);
      });

      const startupTimer = setTimeout(() => {
        if (!resolved) {
          resolved = true;
          reject(new Error("Whisper worker did not acknowledge readiness within 30 seconds"));
          child.kill();
        }
      }, 30000);

      const rl = createInterface({ input: child.stdout });
      rl.on("line", (line) => {
        line = line.trim();
        if (!line) return;

        try {
          const msg = JSON.parse(line);
          if (msg.event === "ready") {
            console.log(`[LocalWhisper] Worker ready (model=${msg.model}, RSS=${msg.rss_mb} MB)`);
            if (!resolved) {
              resolved = true;
              clearTimeout(startupTimer);
              resolve();
            }
            return;
          }

          if (typeof msg.id === "number") {
            const waiting = this.pending.get(msg.id);
            if (!waiting) return;
            clearTimeout(waiting.timer);
            this.pending.delete(msg.id);
            if (msg.error) {
              waiting.reject(new Error(msg.error));
            } else {
              waiting.resolve(msg);
            }
          }
        } catch {
          // Ignore non-JSON lines
        }
      });

      const onExit = (code: number | null, signal: string | null) => {
        console.warn(`[LocalWhisper] Worker exited with code=${code} signal=${signal}`);
        if (this.child === child) {
          this.child = undefined; this.workerReady = undefined;
        }
        for (const req of this.pending.values()) {
          clearTimeout(req.timer);
          req.reject(new Error("Local Whisper worker terminated unexpectedly."));
        }
        this.pending.clear();
        if (!resolved) {
          resolved = true;
          clearTimeout(startupTimer);
          reject(new Error(`Worker failed to start (exit code ${code})`));
        }
      };

      child.on("error", (err) => {
        console.error("[LocalWhisper] Worker process error:", err);
        onExit(null, null);
      });
      child.on("exit", onExit);
    });
    return this.workerReady;
  }

  private async request(method: string, params: Record<string, any> = {}): Promise<any> {
    await this.ensureWorkerStarted();
    const id = ++this.seq;

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Local Whisper request timed out for method: ${method}`));
      }, 600000);

      this.pending.set(id, { resolve, reject, timer });
      const payload = JSON.stringify({ id, method, ...params }) + "\n";
      this.child!.stdin.write(payload, "utf-8", (err) => {
        if (err && this.pending.delete(id)) {
          clearTimeout(timer);
          reject(err);
        }
      });
    });
  }

  /**
   * Starts hardware recording on MK20.
   */
  public async start(captureId: string): Promise<void> {
    const generation=++this.captureGeneration;
    this.currentCaptureId = captureId;
    this.activeRecording = true;

    try {
      await Promise.all([this.ensureWorkerStarted(), this.audioTransport.startDeviceRecording()]);
      if(generation!==this.captureGeneration||!this.activeRecording||this.currentCaptureId!==captureId)throw Error('Capture cancelled during microphone startup');
    } catch (error) {
      if(generation!==this.captureGeneration)throw error;
      this.activeRecording = false; this.currentCaptureId = undefined;
      await this.audioTransport.cancelDeviceRecording().catch(() => {});
      throw error;
    }
  }

  /**
   * Stops recording on MK20, pulls and extracts Channel 3 mono WAV,
   * transcribes locally using resident Whisper model, and returns text.
   */
  public async finish(captureId: string): Promise<string> {
    if (!this.activeRecording || this.currentCaptureId !== captureId) {
      throw new Error("Capture was not active for this capture ID.");
    }
    this.activeRecording = false;

    await this.audioTransport.stopDeviceRecording();
    const wavPath = await this.audioTransport.pullDeviceWav();

    try {
      console.log(`[LocalWhisper] Transcribing ${wavPath} with resident Whisper model...`);
      const resp = await this.request("transcribe", { wavPath });
      const text = typeof resp.text === "string" ? resp.text.trim() : "";
      console.log(`[LocalWhisper] Transcription result (${resp.durationMs}ms, lang=${resp.language}, ${text.length} characters)`);
      return text;
    } finally {
      try {
        fs.rmSync(wavPath, { force: true });
        if(path.basename(path.dirname(wavPath)).startsWith("snowball-voice-"))fs.rmdirSync(path.dirname(wavPath));
      } catch {}
      if(this.currentCaptureId===captureId)this.currentCaptureId = undefined;
    }
  }

  /**
   * Cancels in-flight recording and cleans up temporary resources.
   */
  public async cancel(captureId: string): Promise<void> {
    if(this.currentCaptureId!==captureId)return;
    this.captureGeneration++;
    console.log(`[LocalWhisper] Cancelling capture for ${captureId}...`);
    this.activeRecording = false;
    this.currentCaptureId = undefined;

    await this.audioTransport.cancelDeviceRecording();
  }

  /**
   * Read-only worker status snapshot.
   */
  public async status(): Promise<string> {
    try {
      const res = await this.request("status");
      return JSON.stringify(res);
    } catch (e: any) {
      return `Worker status unavailable: ${e.message}`;
    }
  }

  /**
   * Releases any stuck capture state.
   */
  public async resetStuck(): Promise<void> {
    this.captureGeneration++;
    this.activeRecording = false;
    this.currentCaptureId = undefined;
    await this.audioTransport.cancelDeviceRecording().catch(() => {});
  }

  public close(): void {
    if (this.child) {
      try {
        this.child.stdin.end();
        this.child.kill();
      } catch {}
      this.child = undefined; this.workerReady = undefined;
    }
    this.audioTransport.stopDeviceRecording().catch(() => {});
  }
}
