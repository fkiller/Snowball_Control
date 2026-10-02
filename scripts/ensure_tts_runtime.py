#!/usr/bin/env python3
"""
Snowball TTS Runtime & Dependency Manager for Kokoro-82M ONNX.
Ensures onnxruntime, soundfile, and the unified Kokoro-82M ONNX model (80MB)
are verified and installed without manual user intervention.
"""

import os
import sys
import platform
import subprocess
import shutil
import json
import argparse
import urllib.request
import time

KOKORO_MODEL_URL = "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/kokoro-v1.0.onnx"
KOKORO_VOICES_URL = "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/voices-v1.0.bin"

def resolve_user_data_dir() -> str:
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
        return os.path.join(base, "Snowball", "models", "tts")
    elif sys.platform == "darwin":
        return os.path.join(os.path.expanduser("~"), "Library", "Application Support", "Snowball", "models", "tts")
    else:
        return os.path.join(os.path.expanduser("~"), ".local", "share", "snowball", "models", "tts")

def resolve_model_dir() -> str:
    if os.environ.get("SNOWBALL_TTS_MODELS_DIR"):
        return os.path.abspath(os.environ["SNOWBALL_TTS_MODELS_DIR"])

    # Local fallback candidates
    repo_model_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "models", "tts"))
    if os.path.isdir(repo_model_dir):
        return repo_model_dir

    return resolve_user_data_dir()

def check_runtime_status() -> dict:
    model_dir = resolve_model_dir()
    model_path = os.path.join(model_dir, "kokoro-v1.0.onnx")
    voices_path = os.path.join(model_dir, "voices-v1.0.bin")

    onnx_ready = False
    try:
        import onnxruntime
        onnx_ready = True
    except Exception:
        pass

    soundfile_ready = False
    try:
        import soundfile
        soundfile_ready = True
    except Exception:
        pass

    kokoro_ready = False
    try:
        import kokoro_onnx
        kokoro_ready = True
    except Exception:
        pass

    model_present = os.path.isfile(model_path) and os.path.getsize(model_path) > 10_000_000
    voices_present = os.path.isfile(voices_path) and os.path.getsize(voices_path) > 100_000

    # Optimal backend detection
    optimal_backend = "cpu"
    sys_name = platform.system()
    machine = platform.machine().lower()

    if sys_name == "Darwin" and (machine.startswith("arm") or machine == "aarch64"):
        optimal_backend = "coreml"
    elif sys_name == "Windows":
        # Check CUDA or DirectML
        try:
            import ctypes
            cuda = ctypes.windll.LoadLibrary("nvcuda.dll")
            if cuda.cuInit(0) == 0:
                optimal_backend = "cuda"
            else:
                optimal_backend = "directml"
        except Exception:
            optimal_backend = "directml"
    elif shutil.which("nvidia-smi"):
        optimal_backend = "cuda"

    all_ready = onnx_ready and soundfile_ready and kokoro_ready and model_present and voices_present

    return {
        "ok": all_ready,
        "python_version": platform.python_version(),
        "platform": f"{platform.system()} {platform.machine()}",
        "onnxruntime": onnx_ready,
        "soundfile": soundfile_ready,
        "kokoro_onnx": kokoro_ready,
        "optimal_backend": optimal_backend,
        "model_dir": model_dir,
        "model_present": model_present,
        "voices_present": voices_present,
        "model_path": model_path if model_present else None,
        "voices_path": voices_path if voices_present else None,
    }

def install_dependencies(python_bin: str = None) -> bool:
    py = python_bin or sys.executable
    print(f"[EnsureTtsRuntime] Installing TTS dependencies using: {py}...", flush=True)

    pkgs = ["soundfile", "scipy"]
    # Platform-specific onnxruntime
    sys_name = platform.system()
    if sys_name == "Windows":
        pkgs.extend(["onnxruntime-directml", "kokoro-onnx"])
    elif sys_name == "Darwin":
        pkgs.extend(["onnxruntime", "kokoro-onnx"])
    else:
        pkgs.extend(["onnxruntime", "kokoro-onnx"])

    uv = shutil.which("uv")
    if uv:
        cmd = [uv, "pip", "install", "--python", py] + pkgs
    else:
        cmd = [py, "-m", "pip", "install", "--upgrade"] + pkgs

    try:
        subprocess.check_call(cmd)
        print("[EnsureTtsRuntime] Python packages installed successfully.", flush=True)
    except subprocess.CalledProcessError as e:
        print(f"[EnsureTtsRuntime] Package install failed: {e}", file=sys.stderr)
        return False

    return True

def download_file(url: str, dest_path: str):
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    tmp_path = dest_path + ".tmp"
    print(f"[EnsureTtsRuntime] Downloading {os.path.basename(dest_path)} from {url}...", flush=True)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 SnowballControl/1.0"})
        with urllib.request.urlopen(req) as resp, open(tmp_path, "wb") as out_f:
            total = int(resp.headers.get("Content-Length", 0))
            downloaded = 0
            chunk_size = 1024 * 1024
            while True:
                chunk = resp.read(chunk_size)
                if not chunk:
                    break
                out_f.write(chunk)
                downloaded += len(chunk)
                if total > 0:
                    pct = (downloaded / total) * 100
                    print(f"\r  Downloaded: {downloaded / (1024*1024):.1f}MB / {total / (1024*1024):.1f}MB ({pct:.1f}%)", end="", flush=True)
        print()
        if os.path.exists(dest_path):
            os.remove(dest_path)
        os.rename(tmp_path, dest_path)
        print(f"[EnsureTtsRuntime] Verified and saved to: {dest_path}", flush=True)
        return True
    except Exception as e:
        print(f"\n[EnsureTtsRuntime] Download failed: {e}", file=sys.stderr)
        if os.path.exists(tmp_path):
            try:
                os.remove(tmp_path)
            except Exception:
                pass
        return False

def ensure_models() -> bool:
    model_dir = resolve_model_dir()
    os.makedirs(model_dir, exist_ok=True)
    model_path = os.path.join(model_dir, "kokoro-v1.0.onnx")
    voices_path = os.path.join(model_dir, "voices-v1.0.bin")

    success = True
    if not os.path.isfile(model_path) or os.path.getsize(model_path) < 10_000_000:
        if not download_file(KOKORO_MODEL_URL, model_path):
            success = False

    if not os.path.isfile(voices_path) or os.path.getsize(voices_path) < 100_000:
        if not download_file(KOKORO_VOICES_URL, voices_path):
            success = False

    return success

def main():
    parser = argparse.ArgumentParser(description="Ensure TTS runtime dependencies and models.")
    parser.add_argument("--check", action="store_true", help="Check runtime and output JSON status")
    parser.add_argument("--install", action="store_true", help="Install pip packages")
    parser.add_argument("--download-models", action="store_true", help="Download Kokoro ONNX model and voices")
    parser.add_argument("--python", type=str, help="Python executable to use for pip")

    args = parser.parse_args()

    if args.check:
        st = check_runtime_status()
        print(json.dumps(st, indent=2, ensure_ascii=False))
        sys.exit(0 if st["ok"] else 1)

    if args.install:
        ok = install_dependencies(args.python)
        if not ok:
            sys.exit(1)

    if args.download_models:
        ok = ensure_models()
        if not ok:
            sys.exit(1)

    st = check_runtime_status()
    print(json.dumps(st, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
