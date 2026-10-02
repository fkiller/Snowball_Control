"""
Headless CPU Speech Transcription Worker for Snowball Control.
Supports both standalone file transcription and resident stdio JSON-RPC daemon.
Zero GUI, zero hotkey injection, zero external cloud dependency.
Optimized for high-accuracy CPU batch transcription (large-v3-turbo, VAD, repetition penalties).
"""

import argparse
import ctypes
import json
import os
import sys
import time

# Ensure UTF-8 streams on Windows
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

def configure_windows_dll_paths():
    """Register nvidia pip wheel bin directories with Windows DLL loader."""
    if sys.platform != "win32":
        return
    candidates = []
    try:
        import site
        if hasattr(site, "getsitepackages"):
            candidates.extend(site.getsitepackages())
        if hasattr(site, "getusersitepackages"):
            u = site.getusersitepackages()
            if isinstance(u, str):
                candidates.append(u)
    except Exception:
        pass

    prefix_sp = os.path.join(sys.prefix, "Lib", "site-packages")
    if prefix_sp not in candidates:
        candidates.append(prefix_sp)

    for sp in candidates:
        if not sp or not os.path.isdir(sp):
            continue
        for sub in ["nvidia/cublas/bin", "nvidia/cudnn/bin", "nvidia/cuda_nvrtc/bin"]:
            p = os.path.join(sp, sub.replace("/", os.sep))
            if os.path.isdir(p):
                try:
                    os.add_dll_directory(p)
                    os.environ["PATH"] = p + os.pathsep + os.environ.get("PATH", "")
                except Exception:
                    pass

configure_windows_dll_paths()

def probe_cublas() -> bool:
    """Probes if cuBLAS DLLs can be loaded into memory."""
    configure_windows_dll_paths()
    for dll_name in ["cublas64_12.dll", "cublas64_11.dll", "libcublas.so.12", "libcublas.so"]:
        try:
            if sys.platform == "win32":
                ctypes.windll.LoadLibrary(dll_name)
            else:
                ctypes.CDLL(dll_name)
            return True
        except Exception:
            pass
    return False

DEFAULT_INITIAL_PROMPT = "안녕하세요. Python, JavaScript, English, Korean 코딩 및 개발 명령어 음성입니다."
DEFAULT_CPU_THREADS = int(os.environ.get("SNOWBALL_WHISPER_THREADS", "16"))
DEFAULT_BEAM_SIZE = int(os.environ.get("SNOWBALL_WHISPER_BEAM_SIZE", "1"))

def get_rss_mb() -> float:
    try:
        import psutil
        return psutil.Process(os.getpid()).memory_info().rss / (1024 * 1024)
    except Exception:
        return 0.0

def download_model_cli(model_size: str, model_dir: str = None, check_only: bool = False) -> int:
    from faster_whisper import download_model
    target_dir = os.path.abspath(model_dir) if model_dir else None
    if target_dir and not os.path.exists(target_dir):
        os.makedirs(target_dir, exist_ok=True)

    sys.stderr.write(f"[WhisperWorker] Probing model '{model_size}' in '{target_dir or 'default cache'}'...\n")
    try:
        path = download_model(model_size, output_dir=target_dir, local_files_only=True)
        sys.stderr.write(f"[WhisperWorker] Model '{model_size}' is verified present locally at: {path}\n")
        print(json.dumps({"status": "already_downloaded", "model": model_size, "path": path}, ensure_ascii=False), flush=True)
        return 0
    except Exception:
        pass

    if check_only:
        sys.stderr.write(f"[WhisperWorker] Model '{model_size}' is NOT present locally.\n")
        print(json.dumps({"status": "missing", "model": model_size, "target_dir": target_dir}, ensure_ascii=False), flush=True)
        return 1

    sys.stderr.write(f"[WhisperWorker] Downloading model '{model_size}' to '{target_dir or 'default cache'}'...\n")
    t0 = time.time()
    try:
        path = download_model(model_size, output_dir=target_dir, local_files_only=False)
        duration = round(time.time() - t0, 2)
        sys.stderr.write(f"[WhisperWorker] Download complete in {duration}s -> {path}\n")
        print(json.dumps({"status": "downloaded", "model": model_size, "path": path, "duration_sec": duration}, ensure_ascii=False), flush=True)
        return 0
    except Exception as e:
        sys.stderr.write(f"[WhisperWorker] Model download failed: {e}\n")
        print(json.dumps({"status": "error", "model": model_size, "error": str(e)}, ensure_ascii=False), flush=True)
        return 2

def resolve_compute_device(device_arg: str = "auto") -> tuple:
    dev = (device_arg or "auto").lower().strip()
    if dev == "cuda":
        if not probe_cublas():
            sys.stderr.write("[WhisperWorker] WARNING: CUDA requested but cuBLAS (cublas64_12.dll) not found. Falling back to CPU int8.\n")
            return "cpu", "int8"
        return "cuda", "float16"
    if dev == "cpu":
        return "cpu", "int8"
    try:
        import ctranslate2
        if ctranslate2.get_cuda_device_count() > 0 and probe_cublas():
            return "cuda", "float16"
    except Exception:
        pass
    return "cpu", "int8"

def run_cli(model_size: str, wav_path: str, model_dir: str = None, device_arg: str = "auto"):
    from faster_whisper import WhisperModel
    device, compute_type = resolve_compute_device(device_arg)
    sys.stderr.write(f"[WhisperWorker] Loading model {model_size} ({device.upper()}, {compute_type}, threads={DEFAULT_CPU_THREADS}, dir={model_dir})...\n")
    t0 = time.time()
    try:
        model = WhisperModel(model_size, device=device, compute_type=compute_type, cpu_threads=DEFAULT_CPU_THREADS, download_root=model_dir)
    except Exception as e:
        if device == "cuda":
            sys.stderr.write(f"[WhisperWorker] Failed to initialize CUDA model: {e}. Falling back to CPU...\n")
            device, compute_type = "cpu", "int8"
            model = WhisperModel(model_size, device=device, compute_type=compute_type, cpu_threads=DEFAULT_CPU_THREADS, download_root=model_dir)
        else:
            raise
    load_sec = time.time() - t0
    sys.stderr.write(f"[WhisperWorker] Loaded in {load_sec:.2f}s ({device.upper()}, RSS: {get_rss_mb():.1f} MB)\n")

    t1 = time.time()
    try:
        segments, info = model.transcribe(
            wav_path,
            beam_size=DEFAULT_BEAM_SIZE,
            vad_filter=True,
            vad_parameters=dict(min_silence_duration_ms=500),
            condition_on_previous_text=False,
            repetition_penalty=1.2,
            no_repeat_ngram_size=3,
            initial_prompt=DEFAULT_INITIAL_PROMPT
        )
    except Exception as e:
        if device == "cuda":
            sys.stderr.write(f"[WhisperWorker] CUDA transcribe failed: {e}. Falling back to CPU...\n")
            device = "cpu"
            model = WhisperModel(model_size, device="cpu", compute_type="int8", cpu_threads=DEFAULT_CPU_THREADS, download_root=model_dir)
            segments, info = model.transcribe(
                wav_path,
                beam_size=DEFAULT_BEAM_SIZE,
                vad_filter=True,
                vad_parameters=dict(min_silence_duration_ms=500),
                condition_on_previous_text=False,
                repetition_penalty=1.2,
                no_repeat_ngram_size=3,
                initial_prompt=DEFAULT_INITIAL_PROMPT
            )
        else:
            raise

    text = " ".join(s.text for s in segments).strip()
    trans_sec = time.time() - t1

    result = {
        "text": text,
        "device": device,
        "language": info.language,
        "language_prob": round(info.language_probability, 3),
        "duration_sec": round(trans_sec, 3),
        "rss_mb": round(get_rss_mb(), 1)
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))

def run_worker(model_size: str, model_dir: str = None, device_arg: str = "auto"):
    from faster_whisper import WhisperModel
    device, compute_type = resolve_compute_device(device_arg)
    sys.stderr.write(f"[WhisperWorker] Initializing resident worker with model={model_size} ({device.upper()}, {compute_type}, threads={DEFAULT_CPU_THREADS}, dir={model_dir})...\n")
    t0 = time.time()
    try:
        model = WhisperModel(model_size, device=device, compute_type=compute_type, cpu_threads=DEFAULT_CPU_THREADS, download_root=model_dir)
    except Exception as e:
        if device == "cuda":
            sys.stderr.write(f"[WhisperWorker] CUDA worker init failed: {e}. Falling back to CPU...\n")
            device, compute_type = "cpu", "int8"
            model = WhisperModel(model_size, device="cpu", compute_type="int8", cpu_threads=DEFAULT_CPU_THREADS, download_root=model_dir)
        else:
            raise
    load_sec = time.time() - t0
    sys.stderr.write(f"[WhisperWorker] Ready in {load_sec:.2f}s ({device.upper()}, RSS: {get_rss_mb():.1f} MB)\n")

    # Initial ready notification to parent
    ready_msg = {
        "event": "ready",
        "model": model_size,
        "device": device,
        "compute_type": compute_type,
        "rss_mb": round(get_rss_mb(), 1),
        "load_sec": round(load_sec, 2)
    }
    print(json.dumps(ready_msg, ensure_ascii=False), flush=True)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as e:
            sys.stderr.write(f"[WhisperWorker] Failed to parse JSON: {e}\n")
            continue

        req_id = req.get("id")
        method = req.get("method", "")

        try:
            if method == "transcribe":
                wav_path = req.get("wavPath")
                if not wav_path or not os.path.exists(wav_path):
                    resp = {"id": req_id, "error": f"WAV file not found: {wav_path}"}
                else:
                    t_start = time.time()
                    initial_prompt = req.get("initialPrompt") or os.environ.get(
                        "SNOWBALL_WHISPER_INITIAL_PROMPT",
                        DEFAULT_INITIAL_PROMPT
                    )
                    beam_size = int(req.get("beamSize", DEFAULT_BEAM_SIZE))
                    vad_filter = req.get("vadFilter", True)
                    min_silence_ms = int(req.get("minSilenceMs", 500))

                    try:
                        segments, info = model.transcribe(
                            wav_path,
                            beam_size=beam_size,
                            vad_filter=vad_filter,
                            vad_parameters=dict(min_silence_duration_ms=min_silence_ms),
                            condition_on_previous_text=False,
                            repetition_penalty=1.2,
                            no_repeat_ngram_size=3,
                            initial_prompt=initial_prompt
                        )
                    except Exception as trans_err:
                        err_str = str(trans_err).lower()
                        if device == "cuda" and ("cublas" in err_str or "cuda" in err_str or "memory" in err_str):
                            sys.stderr.write(f"[WhisperWorker] CUDA transcribe error ({trans_err}). Falling back to CPU int8...\n")
                            device = "cpu"
                            compute_type = "int8"
                            model = WhisperModel(model_size, device="cpu", compute_type="int8", cpu_threads=DEFAULT_CPU_THREADS, download_root=model_dir)
                            segments, info = model.transcribe(
                                wav_path,
                                beam_size=beam_size,
                                vad_filter=vad_filter,
                                vad_parameters=dict(min_silence_duration_ms=min_silence_ms),
                                condition_on_previous_text=False,
                                repetition_penalty=1.2,
                                no_repeat_ngram_size=3,
                                initial_prompt=initial_prompt
                            )
                        else:
                            raise trans_err

                    text = " ".join(s.text for s in segments).strip()
                    elapsed_ms = int((time.time() - t_start) * 1000)
                    resp = {
                        "id": req_id,
                        "text": text,
                        "device": device,
                        "language": info.language,
                        "durationMs": elapsed_ms,
                        "rss_mb": round(get_rss_mb(), 1)
                    }
            elif method == "status":
                resp = {
                    "id": req_id,
                    "model": model_size,
                    "ready": True,
                    "rss_mb": round(get_rss_mb(), 1)
                }
            elif method == "ping":
                resp = {"id": req_id, "text": "pong"}
            else:
                resp = {"id": req_id, "error": f"Unknown method: {method}"}
        except Exception as err:
            sys.stderr.write(f"[WhisperWorker] Error handling method {method}: {err}\n")
            resp = {"id": req_id, "error": str(err)}

        print(json.dumps(resp, ensure_ascii=False), flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Snowball Headless Whisper Worker")
    parser.add_argument("--model", default=os.environ.get("SNOWBALL_WHISPER_MODEL", "large-v3-turbo"))
    parser.add_argument("--device", default=os.environ.get("SNOWBALL_WHISPER_DEVICE", "auto"), help="Execution device: auto, cuda, or cpu")
    parser.add_argument("--model-dir", default=os.environ.get("SNOWBALL_MODELS_DIR"), help="Local cache directory for Whisper models")
    parser.add_argument("--file", help="Transcribe a single audio file and exit")
    parser.add_argument("--worker", action="store_true", help="Run in stdio JSON-RPC worker mode")
    parser.add_argument("--download-only", action="store_true", help="Download the model if missing and exit")
    parser.add_argument("--check-only", action="store_true", help="Check if model is already downloaded locally")
    args = parser.parse_args()

    if args.check_only:
        code = download_model_cli(args.model, args.model_dir, check_only=True)
        sys.exit(code)
    elif args.download_only:
        code = download_model_cli(args.model, args.model_dir, check_only=False)
        sys.exit(code)
    elif args.file:
        run_cli(args.model, args.file, args.model_dir, args.device)
    else:
        run_worker(args.model, args.model_dir, args.device)
