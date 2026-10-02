"""Read-only static capability inventory. Never reads credentials or opens a connection.

Usage: python host/probes/inspect-desktop-dictation.py <absolute-app.asar> <report.json>
This reports symbol presence, NOT a working external dictation integration.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path


def entries(node, prefix=""):
    for name, item in node.get("files", {}).items():
        name = f"{prefix}/{name}"
        if "files" in item:
            yield from entries(item, name)
        elif not item.get("unpacked") and name.endswith(".js"):
            yield name, item


archive, output = map(Path, sys.argv[1:])
markers = [
    "/codex/dictation-stream-connect-info", "/dictation/stream",
    "handleDictationStreamConnectInfoRequest", "getAuthToken",
    "chatgpt-dictation", "openai-bearer.", "pcm16", "audio.append",
    "session.close", "transcript.final", "transcript.segment",
    "onDictationTranscriptInsert", "onDictationTranscriptSend",
    "microphoneInputDeviceId", "globalDictationHold",
]
report = {"archive": str(archive), "evidence": "static-only", "files": []}
with archive.open("rb") as stream:
    _, header_size, _, json_size = struct.unpack("<4I", stream.read(16))
    tree = json.loads(stream.read(json_size))
    for name, item in entries(tree):
        # Exclude translations and unrelated modules; emit no application source.
        if not (name.startswith("/.vite/build/main-") or
                name.startswith("/webview/assets/app-initial-") or
                name.startswith("/webview/assets/app-primary-") or
                name.startswith("/webview/assets/dictation-audio-worklet-")):
            continue
        stream.seek(8 + header_size + int(item["offset"]))
        content = stream.read(item["size"])
        text = content.decode("utf-8")
        report["files"].append({
            "path": name, "sha256": hashlib.sha256(content).hexdigest(),
            "markers": {m: text.find(m) for m in markers if m in text},
        })
output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(f"Inventoried {len(report['files'])} modules; static evidence only: {output}")
