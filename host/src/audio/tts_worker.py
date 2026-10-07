"""
Headless TTS Speech Synthesis Worker for Snowball Control.
Supports both standalone file synthesis and resident stdio JSON-RPC daemon.
Implements 3-tier fallback chain:
  1. Supertonic GPU (CUDA Execution Provider)
  2. Supertonic CPU (CPU Execution Provider)
  3. OS Native TTS (Windows SAPI / macOS say / Linux espeak)
Zero cloud dependency, 100% on-device local control.
"""

import argparse
import base64
import ctypes
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from typing import Optional, Tuple

# Ensure UTF-8 streams on Windows
if sys.platform == "win32":
    try:
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
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
        if hasattr(site, "getusersitepackages"):
            candidates.append(site.getusersitepackages())
    except Exception:
        pass

    if sys.prefix:
        candidates.append(os.path.join(sys.prefix, "Lib", "site-packages"))

    for sp in candidates:
        if not sp or not os.path.isdir(sp):
            continue
        nvidia_dir = os.path.join(sp, "nvidia")
        if os.path.isdir(nvidia_dir):
            try:
                for root, dirs, files in os.walk(nvidia_dir):
                    if "bin" in dirs:
                        bin_dir = os.path.join(root, "bin")
                        try:
                            os.add_dll_directory(bin_dir)
                            os.environ["PATH"] = bin_dir + os.pathsep + os.environ.get("PATH", "")
                        except Exception:
                            pass
                cudnn_bin = os.path.join(nvidia_dir, "cudnn", "bin")
                if os.path.isdir(cudnn_bin):
                    for f in os.listdir(cudnn_bin):
                        if f.endswith(".dll"):
                            try:
                                ctypes.CDLL(os.path.join(cudnn_bin, f))
                            except Exception:
                                pass
            except Exception:
                pass

configure_windows_dll_paths()

_GPU_TTS_INSTANCE = None
_CPU_TTS_INSTANCE = None
_GPU_INIT_FAILED = False

def resolve_hardware_providers(requested_device: str = "auto") -> list:
    """
    Detect and rank available hardware execution providers.
    Note: For Supertonic's multi-step diffusion pipeline with Python NumPy arrays,
    CPUExecutionProvider (AVX2/AVX-512) achieves ~1.0s synthesis (RTF 0.25) with zero PCIe overhead,
    whereas CUDAExecutionProvider suffers from 200+ Memcpy nodes between Python loop steps.
    Therefore, 'auto' selects the highest-throughput engine (CPU SIMD).
    """
    import onnxruntime as ort
    avail = ort.get_available_providers()
    providers = []

    if requested_device == "cuda" and "CUDAExecutionProvider" in avail:
        providers.append("CUDAExecutionProvider")
    elif requested_device in ("directml", "dml") and "DmlExecutionProvider" in avail:
        providers.append("DmlExecutionProvider")
    elif requested_device in ("coreml", "metal") and "CoreMLExecutionProvider" in avail:
        providers.append("CoreMLExecutionProvider")

    providers.append("CPUExecutionProvider")
    return providers

def get_supertonic_instance(device: str = "auto"):
    """
    Get or initialize a Supertonic TTS instance.
    device: 'auto', 'cpu', 'cuda', 'directml', 'coreml'
    """
    global _GPU_TTS_INSTANCE, _CPU_TTS_INSTANCE, _GPU_INIT_FAILED

    if device in ("auto", "cuda", "directml", "dml", "coreml", "metal"):
        if _GPU_INIT_FAILED:
            return None
        if _GPU_TTS_INSTANCE is not None:
            return _GPU_TTS_INSTANCE
        try:
            providers = resolve_hardware_providers(device)
            sys.stderr.write(f"[TtsWorker] Initializing Supertonic TTS (Active EP: {providers})...\n")
            import supertonic.loader
            supertonic.loader.DEFAULT_ONNX_PROVIDERS = providers
            import supertonic
            tts = supertonic.TTS(model="supertonic-3", auto_download=True)
            _GPU_TTS_INSTANCE = tts
            active = tts.model.vocoder_ort.get_providers()
            sys.stderr.write(f"[TtsWorker] Supertonic TTS initialized successfully (Active: {active}).\n")
            return _GPU_TTS_INSTANCE
        except Exception as e:
            sys.stderr.write(f"[TtsWorker] Supertonic TTS initialization failed: {e}\n")
            _GPU_INIT_FAILED = True
            return None

    elif device == "cpu":
        if _CPU_TTS_INSTANCE is not None:
            return _CPU_TTS_INSTANCE
        try:
            sys.stderr.write("[TtsWorker] Initializing Supertonic TTS (CPU)...\n")
            import supertonic.loader
            supertonic.loader.DEFAULT_ONNX_PROVIDERS = ["CPUExecutionProvider"]
            import supertonic
            tts = supertonic.TTS(model="supertonic-3", auto_download=True)
            _CPU_TTS_INSTANCE = tts
            sys.stderr.write("[TtsWorker] Supertonic CPU initialized successfully.\n")
            return _CPU_TTS_INSTANCE
        except Exception as e:
            sys.stderr.write(f"[TtsWorker] Supertonic CPU initialization failed: {e}\n")
            return None

    return None

def sanitize_tts_text(text: str) -> str:
    """Normalize typography and remove unencodable surrogates."""
    if not text:
        return ""
    clean = text.encode("utf-8", "ignore").decode("utf-8")
    clean = clean.replace("\u2011", "-").replace("\u00a0", " ")
    clean = clean.replace("“", '"').replace("”", '"').replace("‘", "'").replace("’", "'")
    # Replace middle dots with spaces so words don't get glued
    clean = clean.replace("·", " ").replace("•", " ")
    return clean.strip()

def detect_language(text: str, requested_lang: Optional[str] = None) -> str:
    """Detect language if not specified or set to auto."""
    if requested_lang and requested_lang not in ("auto", "en-us", ""):
        # Normalize code (e.g. ko-KR -> ko, en-US -> en)
        return requested_lang.split("-")[0].lower()

    for ch in text:
        code = ord(ch)
        if (0xAC00 <= code <= 0xD7A3) or (0x1100 <= code <= 0x11FF) or (0x3130 <= code <= 0x318F):
            return "ko"

    for ch in text:
        code = ord(ch)
        if (0x3040 <= code <= 0x309F) or (0x30A0 <= code <= 0x30FF):
            return "ja"

    for ch in text:
        code = ord(ch)
        if 0x4E00 <= code <= 0x9FFF:
            return "zh"

    return "en"

def normalize_voice_style(tts, voice: str) -> str:
    """Validate and normalize voice style for Supertonic."""
    valid_styles = ["F1", "F2", "F3", "F4", "F5", "M1", "M2", "M3", "M4", "M5"]
    if voice in valid_styles:
        return voice
    # Map common kokoro/generic voice names to Supertonic
    if any(m in voice.lower() for m in ["male", "man", "adam", "eric", "michael", "daniel"]):
        return "M1"
    return "F1"

def synthesize_with_supertonic(tts, text: str, output_path: str, voice: str = "F1", speed: float = 1.05, lang: str = "ko") -> Tuple[float, int]:
    """Execute Supertonic synthesis."""
    t_s0 = time.time()
    voice_name = normalize_voice_style(tts, voice)
    style = tts.get_voice_style(voice_name)
    t_s1 = time.time()
    wav, dur = tts(text, voice_style=style, total_steps=5, speed=speed, lang=lang)
    t_s2 = time.time()
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    tts.save_audio(wav, output_path)
    t_s3 = time.time()
    sys.stderr.write(f"[TtsPerf] voice_style={t_s1-t_s0:.3f}s, inference={t_s2-t_s1:.3f}s, save_audio={t_s3-t_s2:.3f}s, total={t_s3-t_s0:.3f}s\n")
    return float(dur[0]), tts.sample_rate

def synthesize_with_os_native(text: str, output_path: str, lang: str = "ko") -> Tuple[float, int]:
    """Fallback Tier 3: OS Native TTS."""
    import soundfile as sf
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)

    if sys.platform == "win32":
        encoded_text=base64.b64encode(text.encode("utf-8")).decode("ascii")
        output_literal=os.path.abspath(output_path).replace("'", "''")
        # Text is data, never interpolated as PowerShell code.
        ps_script = f"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$s.SetOutputToWaveFile('{output_literal}')
$text = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{encoded_text}'))
$s.Speak($text)
$s.Dispose()
"""
        b64 = base64.b64encode(ps_script.encode("utf-16le")).decode("ascii")
        res = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-EncodedCommand", b64], capture_output=True, text=True, timeout=120)
        if res.returncode != 0 or not os.path.isfile(output_path):
            raise RuntimeError(f"PowerShell SAPI synthesis failed: {res.stderr}")

    elif sys.platform == "darwin":
        # macOS say command
        res = subprocess.run(["say", "-o", output_path, "--data-format=LEI16@22050", text], capture_output=True, text=True, timeout=120)
        if res.returncode != 0 or not os.path.isfile(output_path):
            raise RuntimeError(f"macOS say synthesis failed: {res.stderr}")

    else:
        # Linux espeak / spd-say
        res = subprocess.run(["espeak", "-w", output_path, text], capture_output=True, text=True, timeout=120)
        if res.returncode != 0 or not os.path.isfile(output_path):
            raise RuntimeError(f"Linux espeak synthesis failed: {res.stderr}")

    data, rate = sf.read(output_path)
    sf.write(output_path, data, rate, format="WAV", subtype="PCM_16")
    info = sf.info(output_path)
    return float(info.duration), int(info.samplerate)

def synthesize_text(
    text: str,
    output_path: str,
    voice: str = "F1",
    speed: float = 1.05,
    lang: str = "auto",
    requested_device: str = "auto"
) -> dict:
    """
    Executes the 3-tier fallback synthesis:
      Tier 1: Supertonic GPU
      Tier 2: Supertonic CPU
      Tier 3: OS Native TTS
    """
    text = sanitize_tts_text(text)
    if not text:
        raise ValueError("Nothing to synthesize (empty text after sanitization)")

    lang = detect_language(text, lang)
    t0 = time.time()
    active_engine = None
    duration_s = 0.0
    sample_rate = 24000
    last_err = None

    # Tier 1 & 2: Supertonic Synthesis (Auto-optimized: CPU SIMD or GPU)
    tts_inst = get_supertonic_instance(requested_device) or get_supertonic_instance("cpu")
    if tts_inst:
        try:
            duration_s, sample_rate = synthesize_with_supertonic(tts_inst, text, output_path, voice=voice, speed=speed, lang=lang)
            active_providers = tts_inst.model.vocoder_ort.get_providers()
            active_engine = "supertonic-gpu" if "CUDAExecutionProvider" in active_providers else "supertonic-cpu"
        except Exception as e:
            sys.stderr.write(f"[TtsWorker] Supertonic synthesis failed: {e}. Falling back to Tier 3 (OS Native TTS)...\n")
            last_err = e

    # Tier 3: OS Native TTS
    if not active_engine:
        try:
            sys.stderr.write("[TtsWorker] Executing Tier 3 (OS Native TTS)...\n")
            duration_s, sample_rate = synthesize_with_os_native(text, output_path, lang=lang)
            active_engine = "os-native"
        except Exception as e:
            sys.stderr.write(f"[TtsWorker] Tier 3 (OS Native TTS) failed: {e}\n")
            raise RuntimeError(f"All TTS synthesis engines in fallback chain failed. Last error: {e or last_err}")

    proc_ms = round((time.time() - t0) * 1000, 1)
    return {
        "ok": True,
        "engine": active_engine,
        "wav_path": os.path.abspath(output_path),
        "duration_ms": round(duration_s * 1000),
        "sample_rate": sample_rate,
        "process_time_ms": proc_ms,
        "text": text,
        "voice": voice,
        "lang": lang
    }

def run_daemon(requested_device: str = "auto"):
    """Run JSON-RPC stdio daemon for low-latency resident synthesis."""
    sys.stderr.write(f"[TtsWorker] Starting stdio JSON-RPC daemon (device={requested_device})...\n")

    # Warm up preferred engine and run dummy inference to pre-compile graph
    tts_inst = get_supertonic_instance(requested_device) or get_supertonic_instance("cpu")
    if tts_inst:
        try:
            st = tts_inst.get_voice_style("F1")
            tts_inst("가", voice_style=st, total_steps=1, lang="ko")
            sys.stderr.write("[TtsWorker] Warmup dummy inference completed successfully.\n")
        except Exception as e:
            sys.stderr.write(f"[TtsWorker] Warmup dummy inference note: {e}\n")

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
                "gpu_ready": _GPU_TTS_INSTANCE is not None and any(p in _GPU_TTS_INSTANCE.model.vocoder_ort.get_providers() for p in ("CUDAExecutionProvider", "DmlExecutionProvider", "CoreMLExecutionProvider")),
                "cpu_ready": _CPU_TTS_INSTANCE is not None or (_GPU_TTS_INSTANCE is not None and _GPU_TTS_INSTANCE.model.vocoder_ort.get_providers() == ["CPUExecutionProvider"]),
                "engine_chain": ["supertonic-gpu", "supertonic-cpu", "os-native"],
                "device": requested_device
            }
            print(json.dumps({"jsonrpc": "2.0", "result": st, "id": req_id}), flush=True)
            continue

        if method == "shutdown":
            print(json.dumps({"jsonrpc": "2.0", "result": "bye", "id": req_id}), flush=True)
            break

        if method == "synthesize":
            text = params.get("text", "")
            out_path = params.get("output_path") or os.path.join(os.environ.get("TEMP", "/tmp"), f"tts_{int(time.time()*1000)}.wav")
            voice = params.get("voice", "F1")
            speed = float(params.get("speed", 1.05))
            lang = params.get("lang", "auto")

            try:
                res = synthesize_text(
                    text=text,
                    output_path=out_path,
                    voice=voice,
                    speed=speed,
                    lang=lang,
                    requested_device=requested_device
                )
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
    parser = argparse.ArgumentParser(description="Supertonic TTS Speech Synthesis Worker with 3-tier Fallback")
    parser.add_argument("--daemon", action="store_true", help="Run as stdio JSON-RPC daemon")
    parser.add_argument("--text", type=str, help="Text to synthesize (standalone CLI mode)")
    parser.add_argument("--output", type=str, help="Output WAV path (standalone CLI mode)")
    parser.add_argument("--voice", type=str, default="F1", help="Voice style (F1-F5, M1-M5)")
    parser.add_argument("--speed", type=float, default=1.05, help="Speech speed multiplier")
    parser.add_argument("--lang", type=str, default="auto", help="Language code (ko, en, ja, zh, etc.)")
    parser.add_argument("--device", type=str, default="auto", help="Compute device: auto, cuda, or cpu")

    args = parser.parse_args()

    if args.daemon:
        run_daemon(args.device)
    elif args.text and args.output:
        res = synthesize_text(
            text=args.text,
            output_path=args.output,
            voice=args.voice,
            speed=args.speed,
            lang=args.lang,
            requested_device=args.device
        )
        print(json.dumps(res, indent=2, ensure_ascii=False))
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
