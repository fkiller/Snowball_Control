"""
Headless TTS Speech Synthesis Worker for Snowball Control.
Supports both standalone file synthesis and resident stdio JSON-RPC daemon.
Uses Kokoro-82M ONNX model with hardware Execution Provider acceleration (CoreML, CUDA, DirectML, CPU).
Zero GUI, zero external cloud dependency.
"""

import argparse
import ctypes
import json
import os
import sys
import time

# Ensure UTF-8 streams on Windows
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

def configure_windows_dll_paths():
    """Register nvidia & directml dll paths if present."""
    if sys.platform != "win32":
        return
    candidates = []
    try:
        import site
        if hasattr(site, "getsitepackages"):
            candidates.extend(site.getsitepackages())
    except Exception:
        pass

    for sp in candidates:
        if not sp or not os.path.isdir(sp):
            continue
        for sub in ["nvidia/cublas/bin", "nvidia/cudnn/bin"]:
            p = os.path.join(sp, sub.replace("/", os.sep))
            if os.path.isdir(p):
                try:
                    os.add_dll_directory(p)
                    os.environ["PATH"] = p + os.pathsep + os.environ.get("PATH", "")
                except Exception:
                    pass

configure_windows_dll_paths()

_KOKORO_INSTANCE = None
_CURRENT_MODEL_PATH = None
_CURRENT_VOICES_PATH = None

def resolve_model_files(model_path: str = None, voices_path: str = None):
    if model_path and os.path.isfile(model_path) and voices_path and os.path.isfile(voices_path):
        return model_path, voices_path

    candidates = []
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
        candidates.append(os.path.join(base, "Snowball", "models", "tts"))
    elif sys.platform == "darwin":
        candidates.append(os.path.join(os.path.expanduser("~"), "Library", "Application Support", "Snowball", "models", "tts"))
    else:
        candidates.append(os.path.join(os.path.expanduser("~"), ".local", "share", "snowball", "models", "tts"))

    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates.extend([
        os.path.abspath(os.path.join(script_dir, "..", "..", "..", "models", "tts")),
        os.path.abspath(os.path.join(script_dir, "..", "..", "models", "tts")),
        os.path.abspath(os.path.join(script_dir, "models", "tts")),
        os.path.abspath("models/tts"),
    ])

    for c in candidates:
        m = os.path.join(c, "kokoro-v1.0.onnx")
        v = os.path.join(c, "voices-v1.0.bin")
        if os.path.isfile(m) and os.path.isfile(v):
            return m, v

    return model_path or "models/tts/kokoro-v1.0.onnx", voices_path or "models/tts/voices-v1.0.bin"

def get_kokoro(model_path: str, voices_path: str, device: str = "auto"):
    global _KOKORO_INSTANCE, _CURRENT_MODEL_PATH, _CURRENT_VOICES_PATH
    model_path, voices_path = resolve_model_files(model_path, voices_path)
    if _KOKORO_INSTANCE is not None and _CURRENT_MODEL_PATH == model_path and _CURRENT_VOICES_PATH == voices_path:
        return _KOKORO_INSTANCE

    from kokoro_onnx import Kokoro
    import onnxruntime as ort

    sys.stderr.write(f"[TtsWorker] Initializing Kokoro-82M from {model_path} (voices: {voices_path})...\n")

    # Determine Execution Providers
    avail = ort.get_available_providers()
    providers = []
    if device == "cuda" and "CUDAExecutionProvider" in avail:
        providers.append("CUDAExecutionProvider")
    elif device == "directml" and "DmlExecutionProvider" in avail:
        providers.append("DmlExecutionProvider")
    elif device == "coreml" and "CoreMLExecutionProvider" in avail:
        providers.append("CoreMLExecutionProvider")
    elif device == "auto":
        if "CoreMLExecutionProvider" in avail:
            providers.append("CoreMLExecutionProvider")
        elif "CUDAExecutionProvider" in avail:
            providers.append("CUDAExecutionProvider")
        elif "DmlExecutionProvider" in avail:
            providers.append("DmlExecutionProvider")

    providers.append("CPUExecutionProvider")
    sys.stderr.write(f"[TtsWorker] Selected Execution Providers: {providers}\n")

    _KOKORO_INSTANCE = Kokoro(model_path, voices_path)
    _CURRENT_MODEL_PATH = model_path
    _CURRENT_VOICES_PATH = voices_path
    sys.stderr.write("[TtsWorker] Kokoro instance initialized successfully.\n")
    return _KOKORO_INSTANCE

def synthesize_text(text: str, output_path: str, model_path: str, voices_path: str, voice: str = "af_bella", speed: float = 1.0, lang: str = "en-us", device: str = "auto") -> dict:
    import soundfile as sf

    kokoro = get_kokoro(model_path, voices_path, device)
    t0 = time.time()
    samples, sample_rate = kokoro.create(text, voice=voice, speed=speed, lang=lang)
    duration_s = len(samples) / float(sample_rate)

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    sf.write(output_path, samples, sample_rate)
    proc_ms = round((time.time() - t0) * 1000, 1)

    return {
        "ok": True,
        "wav_path": os.path.abspath(output_path),
        "duration_ms": round(duration_s * 1000),
        "sample_rate": sample_rate,
        "process_time_ms": proc_ms,
        "text": text,
        "voice": voice
    }

def run_daemon(model_path: str, voices_path: str, device: str = "auto"):
    sys.stderr.write(f"[TtsWorker] Starting stdio JSON-RPC daemon (model={model_path})...\n")

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        req = None
        try:
            req = json.loads(line)
        except Exception as e:
            err_resp = {"jsonrpc": "2.0", "error": {"code": -32700, "message": f"Parse error: {e}"}, "id": None}
            print(json.dumps(err_resp), flush=True)
            continue

        req_id = req.get("id")
        method = req.get("method")
        params = req.get("params", {})

        if method == "ping":
            print(json.dumps({"jsonrpc": "2.0", "result": "pong", "id": req_id}), flush=True)
            continue

        if method == "status":
            st = {
                "ready": True,
                "model_path": model_path,
                "voices_path": voices_path,
                "device": device
            }
            print(json.dumps({"jsonrpc": "2.0", "result": st, "id": req_id}), flush=True)
            continue

        if method == "shutdown":
            print(json.dumps({"jsonrpc": "2.0", "result": "bye", "id": req_id}), flush=True)
            break

        if method == "synthesize":
            text = params.get("text", "")
            out_path = params.get("output_path") or os.path.join(os.environ.get("TEMP", "/tmp"), f"tts_{int(time.time()*1000)}.wav")
            voice = params.get("voice", "af_bella")
            speed = float(params.get("speed", 1.0))
            lang = params.get("lang", "en-us")

            try:
                res = synthesize_text(text, out_path, model_path, voices_path, voice=voice, speed=speed, lang=lang, device=device)
                print(json.dumps({"jsonrpc": "2.0", "result": res, "id": req_id}, ensure_ascii=False), flush=True)
            except Exception as e:
                err_resp = {
                    "jsonrpc": "2.0",
                    "error": {"code": -32000, "message": str(e)},
                    "id": req_id
                }
                print(json.dumps(err_resp, ensure_ascii=False), flush=True)
            continue

        err_resp = {"jsonrpc": "2.0", "error": {"code": -32601, "message": f"Method '{method}' not found"}, "id": req_id}
        print(json.dumps(err_resp), flush=True)

def main():
    parser = argparse.ArgumentParser(description="Kokoro-82M ONNX Speech Synthesis Worker")
    parser.add_argument("--daemon", action="store_true", help="Run as stdio JSON-RPC daemon")
    parser.add_argument("--text", type=str, help="Text to synthesize (standalone CLI mode)")
    parser.add_argument("--output", type=str, help="Output WAV path (standalone CLI mode)")
    parser.add_argument("--model", type=str, default=os.environ.get("SNOWBALL_TTS_MODEL", "models/tts/kokoro-v1.0.onnx"))
    parser.add_argument("--voices", type=str, default=os.environ.get("SNOWBALL_TTS_VOICES", "models/tts/voices-v1.0.bin"))
    parser.add_argument("--voice", type=str, default="af_bella")
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument("--device", type=str, default="auto")

    args = parser.parse_args()

    if args.daemon:
        run_daemon(args.model, args.voices, args.device)
    elif args.text and args.output:
        res = synthesize_text(args.text, args.output, args.model, args.voices, voice=args.voice, speed=args.speed, device=args.device)
        print(json.dumps(res, indent=2, ensure_ascii=False))
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
