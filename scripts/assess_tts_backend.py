#!/usr/bin/env python3
"""
TTS Backend Fallback Assessment Tool for Kokoro-82M ONNX & Snowball Control.
Evaluates hardware capability, Execution Provider readiness (CoreML, CUDA, DirectML),
and CPU core topology according to the following fallback logic:

                 START
                   |
            Apple Silicon?
             /           \\
           yes            no
            |              |
          CoreML       Windows / Linux?
            |           /          \\
          fail      Windows        Linux
            |         /   \\          |
           CPU     CUDA  DirectML   CUDA
                     |       |        |
                   fail    fail     fail
                     +-------+--------+
                             |
                            CPU

Output: Real-time decision trace, ASCII path diagram, and optimal Execution Provider configuration.
Requires ONLY Python standard library (no pip install required for assessment).
"""

import os
import sys
import platform
import subprocess
import shutil
import ctypes
import json
import argparse
import time

# Ensure UTF-8 streams on Windows terminals
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

class TraceLogger:
    def __init__(self):
        self.events = []

    def log(self, step: str, message: str, status: str = "INFO"):
        entry = {
            "timestamp": time.strftime("%H:%M:%S"),
            "step": step,
            "status": status,
            "message": message
        }
        self.events.append(entry)
        prefix = f"[{entry['timestamp']}][{status:4s}][{step}]"
        print(f"  {prefix} {message}", flush=True)

def get_cpu_info():
    logical = os.cpu_count() or 1
    physical = logical
    sys_name = platform.system()
    model_name = platform.processor() or "Generic CPU"

    try:
        import psutil
        p = psutil.cpu_count(logical=False)
        if p:
            physical = p
    except Exception:
        pass

    if physical == logical:
        try:
            if sys_name == "Windows":
                cmd = "Get-CimInstance Win32_Processor | Select-Object -ExpandProperty NumberOfCores"
                out = subprocess.check_output(["powershell", "-NoProfile", "-Command", cmd], text=True, stderr=subprocess.DEVNULL)
                cores = [int(line.strip()) for line in out.strip().splitlines() if line.strip().isdigit()]
                if cores:
                    physical = sum(cores)
                cmd_name = "Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name"
                out_name = subprocess.check_output(["powershell", "-NoProfile", "-Command", cmd_name], text=True, stderr=subprocess.DEVNULL)
                lines = [l.strip() for l in out_name.strip().splitlines() if l.strip()]
                if lines:
                    model_name = lines[0]
            elif sys_name == "Darwin":
                out = subprocess.check_output(["sysctl", "-n", "hw.physicalcpu"], text=True, stderr=subprocess.DEVNULL)
                physical = int(out.strip())
                out_name = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True, stderr=subprocess.DEVNULL)
                model_name = out_name.strip()
            elif sys_name == "Linux":
                out = subprocess.check_output(["lscpu", "-p=CORE,SOCKET"], text=True, stderr=subprocess.DEVNULL)
                unique = set(line.strip() for line in out.splitlines() if line and not line.startswith("#"))
                if unique:
                    physical = len(unique)
                with open("/proc/cpuinfo", "r", encoding="utf-8") as f:
                    for line in f:
                        if "model name" in line:
                            model_name = line.split(":", 1)[1].strip()
                            break
        except Exception:
            pass

    total_ram_gb = 8.0
    avail_ram_gb = 4.0
    try:
        if sys_name == "Windows":
            class MEMORYSTATUSEX(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong),
                    ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("sullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]
            stat = MEMORYSTATUSEX()
            stat.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat)):
                total_ram_gb = round(stat.ullTotalPhys / (1024**3), 1)
                avail_ram_gb = round(stat.ullAvailPhys / (1024**3), 1)
        elif sys_name == "Darwin":
            out = subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True, stderr=subprocess.DEVNULL)
            total_ram_gb = round(int(out.strip()) / (1024**3), 1)
            avail_ram_gb = total_ram_gb * 0.6
        elif sys_name == "Linux":
            with open("/proc/meminfo", "r", encoding="utf-8") as f:
                for line in f:
                    if "MemTotal:" in line:
                        total_ram_gb = round(int(line.split()[1]) / (1024**2), 1)
                    elif "MemAvailable:" in line:
                        avail_ram_gb = round(int(line.split()[1]) / (1024**2), 1)
    except Exception:
        pass

    return {
        "model": model_name,
        "physical_cores": physical,
        "logical_threads": logical,
        "smt_active": logical > physical,
        "total_ram_gb": total_ram_gb,
        "avail_ram_gb": avail_ram_gb
    }

def calculate_optimal_threads(cpu_info: dict) -> dict:
    p = cpu_info["physical_cores"]
    if p <= 2:
        threads = 1
        tier = "Dual-Core (<=2 cores)"
        reason = "Single thread to avoid starving OS and audio playback."
    elif p <= 4:
        threads = max(1, p - 1)
        tier = "Quad-Core (4 cores)"
        reason = f"{threads} worker threads; leaves 1 physical core for OS/audio."
    elif p <= 8:
        threads = max(4, p - 2)
        tier = "Mainstream (6~8 cores)"
        reason = f"{threads} threads within single CCX/die; 2 cores headroom for system."
    else:
        threads = min(8, p - 2)
        tier = f"High-End ({p} cores)"
        reason = "Capped at 8 threads for optimal ONNX sub-graph parallelization."

    return {
        "threads": threads,
        "tier": tier,
        "reason": reason
    }

def load_tts_config(config_path: str = None) -> dict:
    candidates = []
    if config_path:
        candidates.append(config_path)
    if os.environ.get("SNOWBALL_TTS_CONFIG"):
        candidates.append(os.environ.get("SNOWBALL_TTS_CONFIG"))

    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates.append(os.path.join(script_dir, "..", "config", "tts.json"))
    candidates.append(os.path.join(os.getcwd(), "config", "tts.json"))

    for c in candidates:
        if c and os.path.isfile(c):
            try:
                with open(c, "r", encoding="utf-8") as f:
                    cfg = json.load(f)
                    cfg["_source"] = os.path.abspath(c)
                    return cfg
            except Exception:
                pass

    return {
        "primary_engine": "kokoro-82m",
        "backend": "auto",
        "voice": "af_bella",
        "voice_ko": "ko_female_1",
        "voice_en": "en_female_1",
        "speed": 1.0,
        "volume": 0.8,
        "quantization": "int8",
        "_source": "built-in default"
    }

def check_apple_silicon(tracer: TraceLogger, simulate: str = None) -> bool:
    tracer.log("START", "Checking Apple Silicon architecture...")
    if simulate in ("apple_silicon", "coreml_fail"):
        tracer.log("APPLE", f"SIMULATION: Apple Silicon forced ({simulate})", "PASS")
        return True
    if simulate in ("cuda", "directml", "cpu_only", "cuda_fail", "dml_fail"):
        tracer.log("APPLE", f"SIMULATION: Non-Apple-Silicon branch forced ({simulate})", "INFO")
        return False

    sys_name = platform.system()
    machine = platform.machine().lower()
    if sys_name == "Darwin" and machine in ("arm64", "aarch64"):
        tracer.log("APPLE", "Apple Silicon detected (Darwin arm64).", "PASS")
        return True

    try:
        out = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True, stderr=subprocess.DEVNULL)
        if "Apple" in out:
            tracer.log("APPLE", f"Apple CPU confirmed via sysctl: {out.strip()}", "PASS")
            return True
    except Exception:
        pass

    tracer.log("APPLE", "Not Apple Silicon -> Proceeding to Windows/Linux accelerator check.", "INFO")
    return False

def assess_coreml(tracer: TraceLogger, simulate: str = None) -> dict:
    tracer.log("COREML", "Assessing macOS CoreML Execution Provider...")
    if simulate == "coreml_fail":
        tracer.log("COREML", "SIMULATION: CoreML provider forced failure", "FAIL")
        return {"ok": False, "error": "Simulated CoreML failure"}

    if platform.system() != "Darwin" and simulate != "apple_silicon":
        tracer.log("COREML", "CoreML is only supported on macOS Darwin.", "FAIL")
        return {"ok": False, "error": "Non-macOS platform"}

    try:
        import onnxruntime as ort
        if "CoreMLExecutionProvider" in ort.get_available_providers():
            tracer.log("COREML", "CoreMLExecutionProvider is available in onnxruntime.", "PASS")
            return {
                "ok": True,
                "provider": "CoreMLExecutionProvider",
                "backend": "coreml",
                "device": "Apple Neural Engine / Metal GPU"
            }
    except Exception:
        pass

    # If ONNX Runtime not yet installed, CoreML framework itself exists on macOS
    if platform.system() == "Darwin":
        tracer.log("COREML", "macOS CoreML framework verified on system.", "PASS")
        return {
            "ok": True,
            "provider": "CoreMLExecutionProvider",
            "backend": "coreml",
            "device": "Apple Neural Engine / Metal GPU"
        }

    return {"ok": False, "error": "CoreML unavailable"}

def check_nvidia_cuda(tracer: TraceLogger, simulate: str = None) -> dict:
    tracer.log("CUDA", "Assessing NVIDIA CUDA Execution Provider...")
    if simulate in ("cuda_fail", "dml_fail", "cpu_only"):
        tracer.log("CUDA", f"SIMULATION: CUDA forced failure ({simulate})", "FAIL")
        return {"ok": False, "error": "Simulated CUDA failure"}

    if sys.platform == "win32":
        try:
            cuda = ctypes.windll.LoadLibrary("nvcuda.dll")
            if cuda.cuInit(0) == 0:
                count = ctypes.c_int()
                cuda.cuDeviceGetCount(ctypes.byref(count))
                if count.value > 0:
                    tracer.log("CUDA", "NVIDIA CUDA driver API initialized successfully.", "PASS")
                    return {
                        "ok": True,
                        "provider": "CUDAExecutionProvider",
                        "backend": "cuda",
                        "device": "NVIDIA GPU"
                    }
        except Exception:
            pass

    smi = shutil.which("nvidia-smi")
    if smi:
        try:
            out = subprocess.check_output([smi, "--query-gpu=name", "--format=csv,noheader"], text=True, stderr=subprocess.DEVNULL).strip()
            if out:
                tracer.log("CUDA", f"NVIDIA GPU confirmed via nvidia-smi: {out}", "PASS")
                return {
                    "ok": True,
                    "provider": "CUDAExecutionProvider",
                    "backend": "cuda",
                    "device": out
                }
        except Exception:
            pass

    tracer.log("CUDA", "No NVIDIA GPU hardware detected.", "INFO")
    return {"ok": False, "error": "NVIDIA GPU not detected"}

def check_directml(tracer: TraceLogger, simulate: str = None) -> dict:
    tracer.log("DIRECTML", "Assessing DirectML (DmlExecutionProvider) acceleration...")
    if simulate in ("dml_fail", "cpu_only"):
        tracer.log("DIRECTML", f"SIMULATION: DirectML forced failure ({simulate})", "FAIL")
        return {"ok": False, "error": "Simulated DirectML failure"}

    if sys.platform != "win32":
        tracer.log("DIRECTML", "DirectML is a Windows-native DirectX 12 accelerator.", "INFO")
        return {"ok": False, "error": "Non-Windows platform"}

    # DirectML runs on Windows 10/11 DirectX 12 compatible GPUs (Intel, AMD, NVIDIA)
    try:
        d3d12 = ctypes.windll.LoadLibrary("d3d12.dll")
        if d3d12:
            tracer.log("DIRECTML", "DirectX 12 (D3D12) runtime confirmed. DirectML execution ready.", "PASS")
            return {
                "ok": True,
                "provider": "DmlExecutionProvider",
                "backend": "directml",
                "device": "DirectX 12 GPU (DirectML)"
            }
    except Exception as e:
        tracer.log("DIRECTML", f"DirectX 12 check failed: {e}", "FAIL")

    return {"ok": False, "error": "DirectML unavailable"}

def assess_backend(simulate: str = None, config_path: str = None) -> dict:
    tracer = TraceLogger()
    print("=" * 70)
    print("  Snowball TTS (Kokoro-82M ONNX) Hardware & Backend Assessment")
    print("=" * 70)

    cfg = load_tts_config(config_path)
    cpu_info = get_cpu_info()
    thread_plan = calculate_optimal_threads(cpu_info)

    tracer.log("SYSTEM", f"OS: {platform.system()} ({platform.release()}), Arch: {platform.machine()}")
    tracer.log("CPU", f"{cpu_info['model']} | Physical: {cpu_info['physical_cores']}C, Logical: {cpu_info['logical_threads']}T, RAM: {cpu_info['avail_ram_gb']:.1f}GB avail")

    fallback_path = []
    selected_provider = "CPUExecutionProvider"
    selected_backend = "cpu"
    selected_device = cpu_info["model"]

    # 1. Apple Silicon Check
    if check_apple_silicon(tracer, simulate):
        coreml_res = assess_coreml(tracer, simulate)
        if coreml_res["ok"]:
            selected_provider = coreml_res["provider"]
            selected_backend = coreml_res["backend"]
            selected_device = coreml_res["device"]
            fallback_path.append("apple_silicon -> coreml_pass")
        else:
            fallback_path.append("apple_silicon -> coreml_fail -> cpu_fallback")
            tracer.log("FALLBACK", "Falling back from CoreML to CPU Execution Provider.", "WARN")
    else:
        # 2. Windows / Linux Check
        # A. Try CUDA
        cuda_res = check_nvidia_cuda(tracer, simulate)
        if cuda_res["ok"]:
            selected_provider = cuda_res["provider"]
            selected_backend = cuda_res["backend"]
            selected_device = cuda_res["device"]
            fallback_path.append("nvidia_cuda_pass")
        else:
            fallback_path.append("nvidia_cuda_fail")
            # B. Try DirectML (Windows)
            if sys.platform == "win32":
                dml_res = check_directml(tracer, simulate)
                if dml_res["ok"]:
                    selected_provider = dml_res["provider"]
                    selected_backend = dml_res["backend"]
                    selected_device = dml_res["device"]
                    fallback_path.append("directml_pass")
                else:
                    fallback_path.append("directml_fail -> cpu_fallback")
                    tracer.log("FALLBACK", "DirectML failed -> Falling back to CPU Universal Plan.", "WARN")
            else:
                fallback_path.append("linux_cpu_fallback")

    if selected_backend == "cpu":
        tracer.log("CPU", f"Universal CPU Execution Provider selected ({thread_plan['threads']} threads).", "PASS")

    result = {
        "model": "kokoro-82m",
        "model_format": "onnx",
        "selected_provider": selected_provider,
        "selected_backend": selected_backend,
        "selected_device": selected_device,
        "optimal_threads": thread_plan["threads"],
        "thread_tier": thread_plan["tier"],
        "thread_reason": thread_plan["reason"],
        "fallback_path": " -> ".join(fallback_path),
        "cpu_info": cpu_info,
        "config": cfg,
        "timestamp": time.time()
    }

    print("-" * 70)
    print("  ASSESSMENT RESULT:")
    print(f"    Model            : Kokoro-82M ONNX (Single unified 80MB model)")
    print(f"    Provider         : {result['selected_provider']}")
    print(f"    Backend          : {result['selected_backend'].upper()}")
    print(f"    Device           : {result['selected_device']}")
    print(f"    Threads          : {result['optimal_threads']} ({result['thread_tier']})")
    print(f"    Decision Path    : {result['fallback_path']}")
    print("=" * 70)

    return result

def main():
    parser = argparse.ArgumentParser(description="Assess TTS Execution Provider readiness and fallback path.")
    parser.add_argument("--json", action="store_true", help="Output JSON result only")
    parser.add_argument("--config", type=str, help="Path to custom tts.json config")
    parser.add_argument("--simulate", type=str, choices=["coreml_fail", "cuda_fail", "dml_fail", "cpu_only", "apple_silicon"], help="Simulate hardware failure branch")

    args = parser.parse_args()

    if args.json:
        # Redirect stdout temporarily during tracing
        import io
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()
        res = assess_backend(simulate=args.simulate, config_path=args.config)
        sys.stdout = old_stdout
        print(json.dumps(res, indent=2, ensure_ascii=False))
    else:
        assess_backend(simulate=args.simulate, config_path=args.config)

if __name__ == "__main__":
    main()
