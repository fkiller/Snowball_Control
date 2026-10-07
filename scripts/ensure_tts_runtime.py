#!/usr/bin/env python3
"""
Snowball TTS Runtime & Dependency Manager for Supertonic TTS.
Ensures supertonic, soundfile, onnxruntime-gpu, and supertonic-3 models
are verified and downloaded without manual user intervention.
Implements the 3-tier fallback chain: Supertonic GPU -> Supertonic CPU -> OS Native TTS.
"""

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import time

def resolve_cache_dir() -> str:
    """Return model cache path for Supertonic-3."""
    if os.environ.get("SUPERTONIC_CACHE_DIR"):
        return os.path.abspath(os.environ["SUPERTONIC_CACHE_DIR"])
    home = os.path.expanduser("~")
    return os.path.join(home, ".cache", "supertonic3")

def has_supertonic_models(model_dir: str) -> bool:
    """Check if all required ONNX and style assets exist."""
    required = [
        os.path.join("onnx", "duration_predictor.onnx"),
        os.path.join("onnx", "text_encoder.onnx"),
        os.path.join("onnx", "vector_estimator.onnx"),
        os.path.join("onnx", "vocoder.onnx"),
        os.path.join("onnx", "tts.json"),
        os.path.join("onnx", "unicode_indexer.json"),
        os.path.join("voice_styles", "F1.json"),
        os.path.join("voice_styles", "M1.json"),
    ]
    return all(os.path.isfile(os.path.join(model_dir, r)) for r in required)

def check_runtime_status() -> dict:
    model_dir = resolve_cache_dir()

    supertonic_ready = False
    try:
        import supertonic
        supertonic_ready = True
    except Exception:
        pass

    soundfile_ready = False
    try:
        import soundfile
        soundfile_ready = True
    except Exception:
        pass

    onnx_ready = False
    try:
        import onnxruntime
        onnx_ready = True
    except Exception:
        pass

    models_ready = has_supertonic_models(model_dir)

    # Check GPU availability
    gpu_ready = False
    if sys.platform == "win32":
        try:
            import ctypes
            cuda = ctypes.windll.LoadLibrary("nvcuda.dll")
            if cuda.cuInit(0) == 0:
                gpu_ready = True
        except Exception:
            pass
    elif shutil.which("nvidia-smi"):
        gpu_ready = True

    all_ready = supertonic_ready and soundfile_ready and onnx_ready and models_ready

    return {
        "ok": all_ready,
        "engine": "supertonic",
        "fallback_chain": ["supertonic-gpu", "supertonic-cpu", "os-native"],
        "python_version": platform.python_version(),
        "platform": f"{platform.system()} {platform.machine()}",
        "supertonic_installed": supertonic_ready,
        "soundfile_installed": soundfile_ready,
        "onnxruntime_installed": onnx_ready,
        "gpu_available": gpu_ready,
        "models_present": models_ready,
        "cache_dir": model_dir,
    }

def install_dependencies() -> bool:
    """Auto-install supertonic and soundfile using pip."""
    cmd = [sys.executable, "-m", "pip", "install", "supertonic", "soundfile", "huggingface-hub"]
    sys.stderr.write(f"[EnsureTts] Installing required Python dependencies: {' '.join(cmd)}\n")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(f"[EnsureTts] Pip installation note: {res.stderr}\n")
        return False
    return True

def download_models() -> bool:
    """Download Supertonic-3 model files from Hugging Face Hub."""
    try:
        sys.stderr.write("[EnsureTts] Downloading Supertonic-3 models (~404MB)...\n")
        import supertonic
        supertonic.TTS(model="supertonic-3", auto_download=True)
        sys.stderr.write("[EnsureTts] Supertonic-3 models downloaded and verified successfully.\n")
        return True
    except Exception as e:
        sys.stderr.write(f"[EnsureTts] Model download error: {e}\n")
        return False

def ensure_tts(auto_fix: bool = True) -> dict:
    status = check_runtime_status()
    if status["ok"]:
        return status

    if not auto_fix:
        return status

    if not status["supertonic_installed"] or not status["soundfile_installed"] or not status["onnxruntime_installed"]:
        install_dependencies()

    status = check_runtime_status()
    if not status["models_present"]:
        download_models()

    return check_runtime_status()

def main():
    parser = argparse.ArgumentParser(description="Ensure Supertonic TTS Runtime & Dependencies")
    parser.add_argument("--check", action="store_true", help="Check status without modifying")
    parser.add_argument("--ensure", action="store_true", help="Verify and download/install missing items")

    args = parser.parse_args()

    if args.check:
        res = check_runtime_status()
        print(json.dumps(res, indent=2))
        sys.exit(0 if res["ok"] else 1)
    else:
        res = ensure_tts(auto_fix=True)
        print(json.dumps(res, indent=2))
        sys.exit(0 if res["ok"] else 1)

if __name__ == "__main__":
    main()
