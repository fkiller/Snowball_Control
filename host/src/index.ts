import { CodexAdapter } from './codex/adapter.js';
import { LocalWhisperProvider } from './audio/local-whisper.js';
import { LocalKokoroProvider } from './audio/local-kokoro.js';
import { UdpTransport } from './transport/udp.js';
import { MvpController } from './state/mvp-controller.js';
import { fileURLToPath } from 'node:url';

const udp = new UdpTransport();
const tts = new LocalKokoroProvider();
const controller = new MvpController(
  new CodexAdapter(),
  new LocalWhisperProvider(),
  () => {
    udp.sendSync({ getDeviceState: () => controller.state() });
  },
  fileURLToPath(new URL('../../.state/draft.json', import.meta.url)),
  undefined,
  tts
);
await udp.start();
udp.on('device_input', packet => { void controller.input(packet).catch(error => console.error('[Input]', error)); });
const heartbeat = setInterval(() => controller.paint(), 2000);
async function close() { clearInterval(heartbeat); await controller.close(); await udp.stop(); }
process.on('SIGINT', () => { void close().then(() => process.exit(0)); });
process.on('SIGTERM', () => { void close().then(() => process.exit(0)); });
await controller.connect();
