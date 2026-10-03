import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { createInterface } from "node:readline";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import type { NativeTtsProvider, TtsSynthesizeOptions, TtsSynthesizeResult } from "./tts-provider.js";
import { AudioPlayer } from "./player.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export class LocalKokoroProvider implements NativeTtsProvider {
  private child?: ChildProcessWithoutNullStreams;
  private seq = 0;
  private pending = new Map<number, { resolve(value: any): void; reject(error: Error): void; timer: NodeJS.Timeout }>();
  public readonly player = new AudioPlayer();
  private isSpawning = false;

  constructor(
    private modelPath = process.env.SNOWBALL_TTS_MODEL,
    private voicesPath = process.env.SNOWBALL_TTS_VOICES,
    private computeDevice = process.env.SNOWBALL_TTS_DEVICE || "auto"
  ) {}

  public static resolvePythonRuntime(): { exec: string; workerPy: string } {
    const venvPyWin = path.resolve(__dirname, "../../.venv-whisper/Scripts/python.exe");
    const venvPyUnix = path.resolve(__dirname, "../../.venv-whisper/bin/python");
    const candidates = [
      path.resolve(__dirname, "tts_worker.py"),
      path.resolve(__dirname, "../../src/audio/tts_worker.py"),
      path.resolve(process.cwd(), "host/src/audio/tts_worker.py"),
      path.resolve(process.cwd(), "src/audio/tts_worker.py"),
    ];
    const workerPy = candidates.find((c) => fs.existsSync(c)) || candidates[0];

    const venvList = [
      process.env.VIRTUAL_ENV ? path.resolve(process.env.VIRTUAL_ENV, process.platform === "win32" ? "Scripts/python.exe" : "bin/python") : "",
      process.platform === "win32" ? venvPyWin : venvPyUnix,
      "E:/developments/projects/Snowball_Control/host/.venv-whisper/Scripts/python.exe",
    ].filter(Boolean);
    for (const venv of venvList) {
      if (fs.existsSync(venv)) return { exec: venv, workerPy };
    }
    return { exec: "python", workerPy };
  }

  public static async ensureDependencies(): Promise<{ ok: boolean; status: any }> {
    const { exec } = LocalKokoroProvider.resolvePythonRuntime();
    const scriptCandidates = [
      path.resolve(__dirname, "../../../scripts/ensure_tts_runtime.py"),
      path.resolve(process.cwd(), "scripts/ensure_tts_runtime.py"),
      path.resolve(process.cwd(), "../scripts/ensure_tts_runtime.py"),
    ];
    const script = scriptCandidates.find((s) => fs.existsSync(s));
    if (!script) {
      return { ok: true, status: { note: "Runtime script not found, assuming pre-configured" } };
    }

    return new Promise<{ ok: boolean; status: any }>((resolve) => {
      const child = spawn(exec, [script, "--check"], { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
      let out = "";
      child.stdout.on("data", (d) => (out += d.toString()));
      child.on("exit", (code) => {
        try {
          resolve({ ok: code === 0, status: JSON.parse(out) });
        } catch {
          resolve({ ok: false, status: {} });
        }
      });
      child.on("error", () => resolve({ ok: false, status: {} }));
    });
  }

  private async ensureWorker(): Promise<ChildProcessWithoutNullStreams> {
    if (this.child && !this.child.killed) return this.child;
    if (this.isSpawning) {
      await new Promise((r) => setTimeout(r, 200));
      return this.ensureWorker();
    }

    this.isSpawning = true;
    try {
      const { exec, workerPy } = LocalKokoroProvider.resolvePythonRuntime();
      const args = ["-X", "utf8", "-u", workerPy, "--daemon", "--device", this.computeDevice];
      if (this.modelPath) args.push("--model", this.modelPath);
      if (this.voicesPath) args.push("--voices", this.voicesPath);

      const child = spawn(exec, args, {
        stdio: ["pipe", "pipe", "pipe"],
        windowsHide: true,
        env: {
          ...process.env,
          PYTHONIOENCODING: "utf-8",
          PYTHONUTF8: "1",
        },
      });
      this.child = child;

      const rl = createInterface({ input: child.stdout });
      rl.on("line", (line) => {
        try {
          const resp = JSON.parse(line);
          const reqId = Number(resp.id);
          const entry = this.pending.get(reqId);
          if (entry) {
            clearTimeout(entry.timer);
            this.pending.delete(reqId);
            if (resp.error) entry.reject(new Error(resp.error.message || JSON.stringify(resp.error)));
            else entry.resolve(resp.result);
          }
        } catch {}
      });

      child.on("exit", () => {
        this.child = undefined;
        for (const [id, entry] of this.pending.entries()) {
          clearTimeout(entry.timer);
          entry.reject(new Error("TTS worker process exited unexpectedly"));
        }
        this.pending.clear();
      });

      // Quick ping test
      await this.request("ping", {}, 5000);
      return child;
    } finally {
      this.isSpawning = false;
    }
  }

  private async request<T = any>(method: string, params: any, timeoutMs = 15000): Promise<T> {
    const child = await this.ensureWorker();
    const id = ++this.seq;
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`TTS JSON-RPC request '${method}' timed out after ${timeoutMs}ms`));
      }, timeoutMs);

      this.pending.set(id, { resolve, reject, timer });
      const payload = JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n";
      child.stdin.write(payload);
    });
  }

  public async synthesize(text: string, options?: TtsSynthesizeOptions): Promise<TtsSynthesizeResult> {
    const cleanText = text
      .trim()
      .replace(/[\ud800-\udfff]/g, "")
      .replace(/\u2011/g, "-")
      .replace(/\u00a0/g, " ");
    if (!cleanText) {
      return { wavPath: "", durationMs: 0, sampleRate: 24000, text: "", engine: "mock" };
    }

    const hasKorean = /[\uac00-\ud7af\u1100-\u11ff\u3130-\u318f]/.test(cleanText);
    const targetLang = options?.language || (hasKorean ? "ko" : "en-us");
    const targetVoice = options?.voice || "af_bella";

    const tmpWav = path.join(os.tmpdir(), `snowball_tts_${Date.now()}_${Math.random().toString(36).slice(2, 6)}.wav`);
    const timeoutMs = Math.max(60000, cleanText.length * 150);

    // 1. Synthesize via Kokoro-82M ONNX resident daemon (sole TTS engine)
    try {
      const res = await this.request<{ wav_path: string; duration_ms: number; sample_rate: number }>(
        "synthesize",
        {
          text: cleanText,
          output_path: tmpWav,
          voice: targetVoice,
          speed: options?.speed || 1.0,
          lang: targetLang,
        },
        timeoutMs
      );
      return {
        wavPath: res.wav_path,
        durationMs: res.duration_ms,
        sampleRate: res.sample_rate || 24000,
        text: cleanText,
        engine: "kokoro-onnx",
      };
    } catch (err: any) {
      console.error("[LocalKokoroProvider] Kokoro synthesis failed:", err.message);
      return { wavPath: "", durationMs: 0, sampleRate: 24000, text: cleanText, engine: "error" };
    }
  }

  /**
   * Synthesize and immediately play audio on speakers.
   */
  public async speak(text: string, options?: TtsSynthesizeOptions): Promise<void> {
    const res = await this.synthesize(text, options);
    if (res.wavPath && fs.existsSync(res.wavPath)) {
      await this.player.play(res.wavPath, options?.volume ?? 1.0);
    }
  }

  public async stop(): Promise<void> {
    await this.player.stop();
  }

  public close(): void {
    if (this.child && !this.child.killed) {
      try {
        this.child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id: 9999, method: "shutdown" }) + "\n");
      } catch {}
      this.child.kill();
      this.child = undefined;
    }
    void this.player.stop();
  }
}
