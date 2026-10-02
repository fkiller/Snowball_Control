const cp = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

const codexBin = "C:\\Users\\wondo\\AppData\\Local\\OpenAI\\Codex\\bin\\8e5b6932251c2c1c\\codex.exe";
const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "codex-audio-spike-"));

console.log("Audio spike workspace:", tempDir);

const proc = cp.spawn(codexBin, ["--enable", "realtime_conversation", "app-server"], { stdio: ["pipe", "pipe", "inherit"] });
let buf = "";
let threadId = null;
let realtimeSessionStarted = false;

function send(msg) {
  proc.stdin.write(JSON.stringify(msg) + "\n");
}

proc.stdout.on("data", (d) => {
  buf += d.toString("utf8");
  const lines = buf.split("\n");
  buf = lines.pop();
  for (const line of lines) {
    if (!line.trim()) continue;
    try {
      const parsed = JSON.parse(line);
      handleMessage(parsed);
    } catch (e) {
      console.log("JSON Parse err:", e.message, line.slice(0, 100));
    }
  }
});

function handleMessage(msg) {
  if (msg.id) {
    console.log(`RPC [${msg.id}] result:`, msg.result ? "OK" : "ERROR", msg.error ? msg.error.message : "");
    if (msg.id === 1 && msg.result) {
      // Step 2: create disposable thread
      console.log("Step 2: creating disposable thread...");
      send({
        id: 2,
        method: "thread/start",
        params: {
          cwd: tempDir,
          approvalPolicy: "never",
          sandbox: "read-only"
        }
      });
    } else if (msg.id === 2 && msg.result) {
      threadId = msg.result.thread.id;
      console.log("Step 3: thread created:", threadId);
      console.log("Step 4: starting realtime session with clientManagedHandoffs=true, outputModality=text...");
      send({
        id: 3,
        method: "thread/realtime/start",
        params: {
          threadId: threadId,
          outputModality: "text",
          clientManagedHandoffs: true
        }
      });
    } else if (msg.id === 3) {
      console.log("Realtime start RPC response:", msg.result ? "SUCCESS" : msg.error);
    } else if (msg.id === 4) {
      console.log("Append audio RPC response:", msg.result ? "SUCCESS" : msg.error);
    }
  } else if (msg.method) {
    console.log("EVENT:", msg.method, JSON.stringify(msg.params).slice(0, 150));
    if (msg.method === "thread/realtime/started") {
      realtimeSessionStarted = true;
      console.log("REALTIME STARTED! Now sending audio chunk fixture (1 sec 16-bit PCM @ 24kHz)...");
      // 24000 samples/sec, 16-bit mono = 48000 bytes/sec
      // Let's generate a 0.5s PCM buffer (24000 bytes)
      const samples = 12000;
      const pcmBuffer = Buffer.alloc(samples * 2);
      for (let i = 0; i < samples; i++) {
        const val = Math.floor(Math.sin(2 * Math.PI * 440 * (i / 24000)) * 10000);
        pcmBuffer.writeInt16LE(val, i * 2);
      }
      send({
        id: 4,
        method: "thread/realtime/appendAudio",
        params: {
          threadId: threadId,
          audio: {
            data: pcmBuffer.toString("base64"),
            sampleRate: 24000,
            numChannels: 1,
            samplesPerChannel: samples,
            itemId: "audio_chunk_1"
          }
        }
      });

      // After 3 seconds, stop realtime
      setTimeout(() => {
        console.log("Stopping realtime session...");
        send({
          id: 5,
          method: "thread/realtime/stop",
          params: { threadId: threadId }
        });
        setTimeout(() => {
          proc.kill();
          try { fs.rmSync(tempDir, { recursive: true, force: true }); } catch {}
          process.exit(0);
        }, 1500);
      }, 3000);
    }
  }
}

// Step 1: initialize
send({
  id: 1,
  method: "initialize",
  params: {
    clientInfo: { name: "snowball-audio-spike", version: "0.1.0" },
    capabilities: { experimentalApi: true }
  }
});

setTimeout(() => {
  console.log("Timeout reached, shutting down...");
  proc.kill();
  try { fs.rmSync(tempDir, { recursive: true, force: true }); } catch {}
  process.exit(0);
}, 15000);
