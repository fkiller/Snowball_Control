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
  public readonly player: AudioPlayer;
  private isSpawning = false;
  private currentAbort?: AbortController;
  public currentVolume = 0.75;
  public isMuted = false;

  public setVolume(volume: number, isMuted = false): void {
    this.currentVolume = Math.max(0, Math.min(1.0, volume));
    this.isMuted = isMuted;
    this.player.setVolume(this.currentVolume, isMuted);
  }

  constructor(
    private computeDevice = process.env.SNOWBALL_TTS_DEVICE || "auto",
    player = new AudioPlayer()
  ) { this.player = player; }

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
      process.env.PYTHON_BIN || "",
      process.env.VIRTUAL_ENV ? path.resolve(process.env.VIRTUAL_ENV, process.platform === "win32" ? "Scripts/python.exe" : "bin/python") : "",
      process.platform === "win32" ? venvPyWin : venvPyUnix,
    ].filter(Boolean);
    for (const venv of venvList) {
      if (fs.existsSync(venv)) return { exec: venv, workerPy };
    }
    return { exec: process.platform === "win32" ? "python" : "python3", workerPy };
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
      return { ok: false, status: { error: "TTS runtime verification script not found" } };
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
      const status=await this.request<{ready:boolean}>("status",{},15000);
      return status.ready===true;
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
      throw Error("No speech text to synthesize");
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

      if(!res.ok||res.wav_path!==path.resolve(tmpWav)||!fs.existsSync(res.wav_path)||!Number.isFinite(res.duration_ms)||res.duration_ms<=0||!['supertonic-gpu','supertonic-cpu','os-native'].includes(res.engine))throw Error("TTS worker returned no verifiable audio");
      const procMs = res.process_time_ms || 0;
      const rtf = res.duration_ms > 0 ? (procMs / res.duration_ms).toFixed(2) : "0.00";
      console.log(`[Supertonic] Synth: "${cleanText.length} characters" -> ${res.duration_ms}ms audio in ${procMs}ms (RTF: ${rtf}, ${res.engine})`);

      return {
        wavPath: res.wav_path,
        durationMs: res.duration_ms,
        sampleRate: res.sample_rate || 44100,
        text: cleanText,
        engine: res.engine,
      };
    } catch (err: any) {
      try {fs.rmSync(tmpWav,{force:true});} catch {}
      throw new Error("Local TTS synthesis failed: " + err.message);
    }
  }

  /**
   * Synthesize and immediately play audio on speakers with pipelined sentence streaming.
   * Synthesizes the first sentence and starts playback after native synthesis completes,
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

    if (options?.volume !== undefined) {
      this.currentVolume = Math.max(0, Math.min(1.0, options.volume));
    }

    // Set up cancellation token for this speech invocation
    this.currentAbort?.abort();
    const abort = new AbortController();
    this.currentAbort = abort;

    const playChunk = async (wavPath: string) => {
      const vol = this.isMuted ? 0 : this.currentVolume;
      if (options?.destination === "device") {
        await this.player.playOnDevice(wavPath, vol);
      } else {
        await this.player.play(wavPath, vol);
      }
    };

    if (sentences.length === 1) {
      const res = await this.synthesize(sentences[0], options);
      if (abort.signal.aborted) {fs.rmSync(res.wavPath,{force:true});return;}
      if (res.wavPath && fs.existsSync(res.wavPath)) {
        try {await playChunk(res.wavPath);} finally {fs.rmSync(res.wavPath,{force:true});}
      }
      return;
    }

    // Resolve/reject prefetch explicitly so failed background synthesis cannot
    // become an unhandled rejection while the preceding sentence is playing.
    const synth=(sentence: string)=>this.synthesize(sentence,options).then(result=>({result}),error=>({error}));
    let pending: ReturnType<typeof synth> | undefined=synth(sentences[0]);
    try {
      for(let i=0;i<sentences.length;i++){
        const output=await pending!;pending=undefined;
        if("error" in output)throw output.error;
        const res=output.result;
        if(abort.signal.aborted){fs.rmSync(res.wavPath,{force:true});break;}
        if(i+1<sentences.length)pending=synth(sentences[i+1]);
        try{await playChunk(res.wavPath);}finally{fs.rmSync(res.wavPath,{force:true});}
      }
    } finally {
      if(pending)void pending.then(output=>{if("result" in output)fs.rmSync(output.result.wavPath,{force:true});});
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
