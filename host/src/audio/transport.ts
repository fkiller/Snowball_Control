import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import * as net from "node:net";
import { audioEndpoint, snauHeader, openAudio, type AudioEndpoint } from "./snau.js";

export class AudioTransport {
  private endpoint?: AudioEndpoint;
  private recordSocket?: net.Socket;
  private recordedChunks: Buffer[] = [];
  private bytes = 0;
  private failure?: Error;
  private generation=0;
  constructor(address = process.env.SNOWBALL_MK20_ADDRESS || "", _maintenanceTool?: string, tcpPort = 7702, lease?: string, localAddress?: string) {
    if(address) this.endpoint = audioEndpoint(address, tcpPort, lease, localAddress);
  }
  public static pcm3chToMonoWav(raw3ch: Buffer): Buffer {
    const nSamples = Math.floor(raw3ch.length / 6);
    const dataSize = nSamples * 2;
    const wav = Buffer.alloc(44 + dataSize);

    // RIFF chunk descriptor
    wav.write("RIFF", 0);
    wav.writeUInt32LE(36 + dataSize, 4);
    wav.write("WAVE", 8);

    // fmt sub-chunk
    wav.write("fmt ", 12);
    wav.writeUInt32LE(16, 16); // Subchunk1Size (16 for PCM)
    wav.writeUInt16LE(1, 20);  // AudioFormat (1 = PCM)
    wav.writeUInt16LE(1, 22);  // NumChannels (1 = Mono)
    wav.writeUInt32LE(16000, 24); // SampleRate (16000 Hz)
    wav.writeUInt32LE(32000, 28); // ByteRate (16000 * 1 * 2)
    wav.writeUInt16LE(2, 32);  // BlockAlign (1 * 2)
    wav.writeUInt16LE(16, 34); // BitsPerSample (16 bits)

    // data sub-chunk
    wav.write("data", 36);
    wav.writeUInt32LE(dataSize, 40);

    // Extract Channel 3 (bytes 4 and 5 of each 6-byte frame)
    let dst = 44;
    for (let i = 0; i < nSamples; i++) {
      const src = i * 6 + 4;
      wav[dst] = raw3ch[src];
      wav[dst + 1] = raw3ch[src + 1];
      dst += 2;
    }

    return wav;
  }

  /**
   * Converts 1-channel 16kHz 16-bit LE PCM data into standard mono 16kHz 16-bit LE WAV.
   */
  public static monoPcmToWav(pcm: Buffer, sampleRate = 16000): Buffer {
    const dataSize = pcm.length;
    const wav = Buffer.alloc(44 + dataSize);

    // RIFF chunk descriptor
    wav.write("RIFF", 0);
    wav.writeUInt32LE(36 + dataSize, 4);
    wav.write("WAVE", 8);

    // fmt sub-chunk
    wav.write("fmt ", 12);
    wav.writeUInt32LE(16, 16); // Subchunk1Size (16 for PCM)
    wav.writeUInt16LE(1, 20);  // AudioFormat (1 = PCM)
    wav.writeUInt16LE(1, 22);  // NumChannels (1 = Mono)
    wav.writeUInt32LE(sampleRate, 24); // SampleRate
    wav.writeUInt32LE(sampleRate * 2, 28); // ByteRate (sampleRate * 1 * 2)
    wav.writeUInt16LE(2, 32);  // BlockAlign (1 * 2)
    wav.writeUInt16LE(16, 34); // BitsPerSample (16 bits)

    // data sub-chunk
    wav.write("data", 36);
    wav.writeUInt32LE(dataSize, 40);
    pcm.copy(wav, 44);

    return wav;
  }


  public async startDeviceRecording(): Promise<void> {
    if(!this.endpoint)throw Error("No selected MK20 audio endpoint");
    if (this.recordSocket) throw Error("MK20 capture already active");
    const generation=++this.generation;
    this.recordedChunks = []; this.bytes = 0; this.failure = undefined;
    const {socket, initial} = await openAudio(this.endpoint, snauHeader(this.endpoint, 2, 1, 100, false, 16000, 0xffffffff));
    if(generation!==this.generation){socket.destroy();throw Error('MK20 capture cancelled before readiness');}
    this.recordSocket = socket;
    const receive = (chunk: Buffer) => {
      this.bytes += chunk.length;
      if (this.bytes > 16000 * 2 * 120) { this.failure = Error("MK20 capture exceeds 120 seconds"); socket.destroy(); return; }
      this.recordedChunks.push(chunk);
    };
    if (initial.length) receive(initial);
    socket.on("data", receive);
    socket.on("error", error => { this.failure = error; });
    socket.setTimeout(5000, () => { this.failure = Error("MK20 audio stream stalled"); socket.destroy(); });
  }
  public async stopDeviceRecording(): Promise<void> {
    this.generation++;
    const socket = this.recordSocket; this.recordSocket = undefined;
    if (!socket || socket.destroyed) return;
    await new Promise<void>(resolve => {
      const timer = setTimeout(() => {socket.destroy(); resolve();}, 1500);
      socket.once("close", () => {clearTimeout(timer); resolve();});
      socket.end(Buffer.from([0]));
    });
  }
  public async pullDeviceWav(): Promise<string> {
    if (this.failure) throw this.failure;
    const pcm = Buffer.concat(this.recordedChunks); this.recordedChunks = [];
    if (pcm.length < 960 || pcm.length % 2) throw Error("MK20 microphone produced insufficient or truncated PCM");
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "snowball-voice-"));
    const wav = path.join(dir, "capture.wav");
    fs.writeFileSync(wav, AudioTransport.monoPcmToWav(pcm), {mode:0o600});
    return wav;
  }
  public async cancelDeviceRecording(): Promise<void> {
    this.generation++;
    this.recordSocket?.destroy(); this.recordSocket = undefined; this.recordedChunks = []; this.bytes = 0;
  }
}
