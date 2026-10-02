"""Bounded MK20 raw PCM transport probe; no recording is retained.

Capability probe: tested MK20 rejects exec-out and lacks base64.
Neither tested transport currently passes. No success is implied by this script.
Never kills other recorders, alters mixer settings or retains microphone audio.
"""
import argparse
import base64
import json
import math
import os
import struct
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--device", default=os.environ.get("SNOWBALL_DEVICE_ADB", "192.168.1.248:5555"))
parser.add_argument("--seconds", type=int, choices=range(1, 6), default=2)
parser.add_argument("--report", type=Path, required=True)
parser.add_argument("--transport", choices=["exec-out", "shell-base64"], default="exec-out")
args = parser.parse_args()
adb = Path(os.environ["LOCALAPPDATA"]) / "Temp/Codex-MK20-ADB/platform-tools/adb.exe"
started = time.monotonic()
command = (["exec-out", "arecord", "-q", "-D", "hw:0,0", "-f", "S16_LE",
            "-r", "16000", "-c", "1", "-t", "raw", "-d", str(args.seconds)]
           if args.transport == "exec-out" else
           ["shell", f"arecord -q -D hw:0,0 -f S16_LE -r 16000 -c 1 -t raw -d {args.seconds} | base64"])
result = subprocess.run(
    [str(adb), "-s", args.device, *command],
    capture_output=True, timeout=args.seconds + 10,
)
data = result.stdout
decode_error = None
if args.transport == "shell-base64":
    try:
        data = base64.b64decode(b"".join(data.split()), validate=True)
    except ValueError:
        data = b""
        decode_error = "Shell did not return valid base64; check encoder availability."
expected = 16000 * 2 * args.seconds
samples = struct.unpack("<" + "h" * (len(data) // 2), data[:len(data) // 2 * 2])
report = {
    "format": "PCM16LE", "sampleRate": 16000, "channels": 1,
    "transport": args.transport,
    "requestedSeconds": args.seconds, "expectedBytes": expected,
    "receivedBytes": len(data), "processExitCode": result.returncode,
    "elapsedSeconds": round(time.monotonic() - started, 3),
    "peak": max((abs(s) for s in samples), default=0),
    "rms": round(math.sqrt(sum(s*s for s in samples) / len(samples)), 2) if samples else 0,
    "transportPass": result.returncode == 0 and len(data) == expected,
    "scope": "capture transport only; not speech quality, virtual microphone or dictation",
    "audioRetained": False,
    "decodeError": decode_error,
}
args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
raise SystemExit(0 if report["transportPass"] else 1)
