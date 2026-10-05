import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { createInterface } from "node:readline";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import type { NativeTtsProvider, TtsSynthesizeOptions, TtsSynthesizeResult } from "./tts-provider.js";
import { AudioPlayer } from "./player.js";
import { cleanTextForSpeech } from "./korean-transliterate.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

/**
 * Splits continuous text into natural sentence chunks for pipelined streaming synthesis.
 */
export function splitIntoSentences(text: string): string[] {
  const clean = text.trim();
  if (!clean) return [];

  // Split on sentence-ending punctuation followed by whitespace or line break
  const rawChunks = clean
    .split(/(?<=[.!?。！？\n])\s+/)
    .map((s) => s.trim())
    .filter(Boolean);

  const sentences: string[] = [];
  let buffer = "";

  for (const chunk of rawChunks) {
    // If buffer + chunk is short (< 40 chars), combine to keep natural speech flow
    if (buffer.length > 0 && buffer.length + chunk.length < 40) {
      buffer += " " + chunk;
    } else {
      if (buffer) sentences.push(buffer);
      buffer = chunk;
    }
  }
  if (buffer) sentences.push(buffer);
  return sentences.length > 0 ? sentences : [clean];
}

export class LocalSupertonicProvider implements NativeTtsProvider {
  private child?: ChildProcessWithoutNullStreams;
  private seq = 0;
  private pending = new Map<number, { resolve(value: any): void; reject(error: Error): void; timer: NodeJS.Timeout }>();
  public readonly player = new AudioPlayer();
  private isSpawning = false;
  private currentAbort?: AbortController;

  constructor(
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
    const { exec } = LocalSupertonicProvider.resolvePythonRuntime();
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

  public async warmup(): Promise<boolean> {
    try {
      await this.ensureWorker();
      return true;
    } catch (e: any) {
      console.warn("[LocalSupertonicProvider] Warmup warning:", e.message);
      return false;
    }
  }

  private async ensureWorker(): Promise<ChildProcessWithoutNullStreams> {
    if (this.child && !this.child.killed) return this.child;
    if (this.isSpawning) {
      await new Promise((r) => setTimeout(r, 200));
      return this.ensureWorker();
    }

    this.isSpawning = true;
    try {
      const { exec, workerPy } = LocalSupertonicProvider.resolvePythonRuntime();
      const args = ["-X", "utf8", "-u", workerPy, "--daemon", "--device", this.computeDevice];

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

      child.stderr.on("data", (data) => {
        const line = data.toString().trim();
        if (line) {
          process.stderr.write(`[TtsWorker] ${line}\n`);
        }
      });

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

      // Ping check to verify worker initialization
      await this.request("ping", {}, 15000);
      return child;
    } finally {
      this.isSpawning = false;
    }
  }

  private async request<T = any>(method: string, params: any, timeoutMs = 30000): Promise<T> {
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
    const cleanText = cleanTextForSpeech(text, options?.language);
    if (!cleanText) {
      return { wavPath: "", durationMs: 0, sampleRate: 44100, text: "", engine: "mock" };
    }

    const hasKorean = /[\uac00-\ud7af\u1100-\u11ff\u3130-\u318f]/.test(cleanText);
    const targetLang = options?.language || (hasKorean ? "ko" : "en");
    const targetVoice = options?.voice || "F1";

    const tmpWav = path.join(os.tmpdir(), `snowball_tts_${Date.now()}_${Math.random().toString(36).slice(2, 6)}.wav`);
    const timeoutMs = Math.max(30000, cleanText.length * 200);

    // 1. Synthesize via Supertonic resident daemon with 3-tier fallback chain
    try {
      const res = await this.request<{
        ok: boolean;
        engine: "supertonic-gpu" | "supertonic-cpu" | "os-native";
        wav_path: string;
        duration_ms: number;
        sample_rate: number;
        process_time_ms?: number;
      }>(
        "synthesize",
        {
          text: cleanText,
          output_path: tmpWav,
          voice: targetVoice,
          speed: options?.speed || 1.05,
          lang: targetLang,
        },
        timeoutMs
      );

      const procMs = res.process_time_ms || 0;
      const rtf = res.duration_ms > 0 ? (procMs / res.duration_ms).toFixed(2) : "0.00";
      console.log(`[Supertonic] Synth: "${cleanText.slice(0, 32)}..." -> ${res.duration_ms}ms audio in ${procMs}ms (RTF: ${rtf}, ${res.engine})`);

      return {
        wavPath: res.wav_path,
        durationMs: res.duration_ms,
        sampleRate: res.sample_rate || 44100,
        text: cleanText,
        engine: res.engine || "supertonic-gpu",
      };
    } catch (err: any) {
      console.warn("[LocalSupertonicProvider] Worker synthesis failed:", err.message, ". Executing emergency OS Native fallback...");
      try {
        if (process.platform === "win32") {
          const { execSync } = await import("node:child_process");
          const psScript = `Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.SetOutputToWaveFile('${tmpWav.replace(/\\/g, "/")}'); $s.Speak('${cleanText.replace(/'/g, "''")}'); $s.Dispose();`;
          const b64 = Buffer.from(psScript, "utf16le").toString("base64");
          execSync(`powershell -NoProfile -NonInteractive -EncodedCommand ${b64}`);
          if (fs.existsSync(tmpWav)) {
            return {
              wavPath: tmpWav,
              durationMs: 2000,
              sampleRate: 22050,
              text: cleanText,
              engine: "os-native",
            };
          }
        }
      } catch (hostErr: any) {
        console.error("[LocalSupertonicProvider] Emergency host fallback error:", hostErr.message);
      }
      return { wavPath: "", durationMs: 0, sampleRate: 44100, text: cleanText, engine: "error" };
    }
  }

  /**
   * Synthesize and immediately play audio on speakers with pipelined sentence streaming.
   * Synthesizes the first sentence and starts playback immediately (< 0.5s),
   * while prefetching subsequent sentences concurrently.
   * 
   * Destination:
   * - "device": Plays through MK20 hardware onboard speaker via ALSA aplay
   * - "host": Plays through host PC/Mac system speakers
   */
  public async speak(text: string, options?: TtsSynthesizeOptions): Promise<void> {
    const cleaned = cleanTextForSpeech(text, options?.language);
    const sentences = splitIntoSentences(cleaned);
    if (sentences.length === 0) return;

    // Set up cancellation token for this speech invocation
    this.currentAbort?.abort();
    const abort = new AbortController();
    this.currentAbort = abort;

    const playChunk = async (wavPath: string) => {
      if (options?.destination === "device") {
        await this.player.playOnDevice(wavPath, options?.volume ?? 1.0);
      } else {
        await this.player.play(wavPath, options?.volume ?? 1.0);
      }
    };

    if (sentences.length === 1) {
      const res = await this.synthesize(sentences[0], options);
      if (abort.signal.aborted) return;
      if (res.wavPath && fs.existsSync(res.wavPath)) {
        await playChunk(res.wavPath);
      }
      return;
    }

    // Pipelined streaming: start synthesizing sentence 0 immediately
    console.log(`[Supertonic] Streaming speech pipeline started (${sentences.length} chunks, target: ${options?.destination || "host"})...`);
    let nextSynthPromise = this.synthesize(sentences[0], options);

    for (let i = 0; i < sentences.length; i++) {
      if (abort.signal.aborted) break;

      const currentRes = await nextSynthPromise;
      if (abort.signal.aborted) break;

      // Start synthesizing the NEXT sentence in the background while the current one is playing
      if (i + 1 < sentences.length) {
        nextSynthPromise = this.synthesize(sentences[i + 1], options);
      }

      // Play current audio chunk
      if (currentRes.wavPath && fs.existsSync(currentRes.wavPath)) {
        await playChunk(currentRes.wavPath);
      }
    }
  }

  public async stop(): Promise<void> {
    if (this.currentAbort) {
      this.currentAbort.abort();
      this.currentAbort = undefined;
    }
    await this.player.stop();
  }

  public close(): void {
    if (this.currentAbort) {
      this.currentAbort.abort();
      this.currentAbort = undefined;
    }
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

// Export backward compatibility alias
export const LocalKokoroProvider = LocalSupertonicProvider;
