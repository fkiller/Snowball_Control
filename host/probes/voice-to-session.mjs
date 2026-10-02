// Pilot voice-to-session: discrete WAV -> STT -> Codex thread (text inject).
// Reverse-engineering result: desktop `send_message_to_thread` is TEXT-ONLY
// (additionalProperties:false, required threadId+prompt — live schema dump),
// voice tools are end/transfer/capture only (no start), realtime duplex needs
// API-key auth. So voice joins a session as transcribed text, never raw audio.
// Usage:
//   node host/probes/voice-to-session.mjs <wav> [--threadId <id>] [--send]
// Default = dry-run (transcribe only, NEVER sends). --send requires threadId
// and means: user armed a DISPOSABLE thread and consents to one text inject.
import { SpeechManager } from "../dist/audio/speech.js";
import { CodexDesktopClient } from "../dist/codex/desktop.js";

const args = process.argv.slice(2);
const wav = args[0];
if (!wav) {
  console.error("usage: node voice-to-session.mjs <wav> [--threadId <id>] [--send]");
  process.exit(2);
}
const ti = args.indexOf("--threadId");
const threadId = ti >= 0 ? args[ti + 1] : null;
const doSend = args.includes("--send");

const speech = new SpeechManager();
const result = await speech.transcribeFile(wav);
console.log(JSON.stringify({ text: result.text, engine: result.engine, durationMs: result.durationMs }));

if (!doSend) {
  console.log("dry-run: NOT sent (pass --threadId <id> --send to inject into disposable thread)");
  process.exit(0);
}
if (!threadId) {
  console.error("--send requires --threadId <disposable-thread-id>");
  process.exit(2);
}
if (!result.text.trim()) {
  console.error("refusing to send empty transcript");
  process.exit(1);
}
const desktop = new CodexDesktopClient();
const ok = await desktop.connect();
if (!ok) {
  console.error("desktop pipe not found");
  process.exit(1);
}
try {
  const res = await desktop.sendMessageToThread(threadId, result.text.trim());
  console.log("SENT:", JSON.stringify(res).slice(0, 500));
} finally {
  desktop.disconnect();
}
