import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AudioTransport } from '../dist/audio/transport.js';

test('pcm3chToMonoWav correctly extracts Channel 3 into valid 16kHz mono WAV', () => {
  // Synthesize 100 frames of 3-channel 16-bit PCM (6 bytes per sample frame)
  const nFrames = 100;
  const raw3ch = Buffer.alloc(nFrames * 6);

  for (let i = 0; i < nFrames; i++) {
    // Channel 1 (index 0, 1) = 0x1111
    raw3ch.writeInt16LE(0x1111, i * 6);
    // Channel 2 (index 2, 3) = 0x2222
    raw3ch.writeInt16LE(0x2222, i * 6 + 2);
    // Channel 3 (index 4, 5) = (i * 10) & 0x7FFF (variable physical mic signal)
    raw3ch.writeInt16LE((i * 10) & 0x7fff, i * 6 + 4);
  }

  const wav = AudioTransport.pcm3chToMonoWav(raw3ch);

  // WAV header validations
  assert.equal(wav.toString('utf-8', 0, 4), 'RIFF');
  assert.equal(wav.readUInt32LE(4), 36 + nFrames * 2);
  assert.equal(wav.toString('utf-8', 8, 12), 'WAVE');
  assert.equal(wav.toString('utf-8', 12, 16), 'fmt ');
  assert.equal(wav.readUInt32LE(16), 16); // Subchunk1Size
  assert.equal(wav.readUInt16LE(20), 1);  // AudioFormat (PCM)
  assert.equal(wav.readUInt16LE(22), 1);  // NumChannels (1 = Mono)
  assert.equal(wav.readUInt32LE(24), 16000); // SampleRate
  assert.equal(wav.readUInt32LE(28), 32000); // ByteRate (16000 * 1 * 2)
  assert.equal(wav.readUInt16LE(32), 2);  // BlockAlign
  assert.equal(wav.readUInt16LE(34), 16); // BitsPerSample
  assert.equal(wav.toString('utf-8', 36, 40), 'data');
  assert.equal(wav.readUInt32LE(40), nFrames * 2); // DataSize

  // Validate extracted audio payload matches Channel 3
  for (let i = 0; i < nFrames; i++) {
    const val = wav.readInt16LE(44 + i * 2);
    const expected = (i * 10) & 0x7fff;
    assert.equal(val, expected, `Sample ${i} mismatch`);
  }
});

test('monoPcmToWav correctly wraps 1-channel PCM into valid 16kHz mono WAV', () => {
  const nSamples = 1600; // 100ms at 16kHz
  const pcm = Buffer.alloc(nSamples * 2);
  for (let i = 0; i < nSamples; i++) {
    pcm.writeInt16LE((i * 7) & 0x7fff, i * 2);
  }

  const wav = AudioTransport.monoPcmToWav(pcm, 16000);
  assert.equal(wav.toString('utf-8', 0, 4), 'RIFF');
  assert.equal(wav.readUInt32LE(4), 36 + nSamples * 2);
  assert.equal(wav.toString('utf-8', 8, 12), 'WAVE');
  assert.equal(wav.toString('utf-8', 12, 16), 'fmt ');
  assert.equal(wav.readUInt32LE(16), 16);
  assert.equal(wav.readUInt16LE(20), 1); // PCM
  assert.equal(wav.readUInt16LE(22), 1); // Mono
  assert.equal(wav.readUInt32LE(24), 16000); // 16kHz
  assert.equal(wav.readUInt32LE(28), 32000); // 16000 * 2
  assert.equal(wav.readUInt16LE(32), 2);
  assert.equal(wav.readUInt16LE(34), 16);
  assert.equal(wav.toString('utf-8', 36, 40), 'data');
  assert.equal(wav.readUInt32LE(40), nSamples * 2);

  // Validate samples match exactly
  for (let i = 0; i < nSamples; i++) {
    assert.equal(wav.readInt16LE(44 + i * 2), (i * 7) & 0x7fff);
  }
});

test('AudioTransport native TCP streaming: captures 16kHz mono PCM and creates WAV with zero ADB', async () => {
  const net = await import('node:net');
  const fs = await import('node:fs');

  let serverReceivedHeader = null;
  let resolveHeaderReceived;
  const headerReceivedPromise = new Promise((r) => { resolveHeaderReceived = r; });

  const mockServer = net.createServer((socket) => {
    socket.on('error', () => {});
    let headerBuf = Buffer.alloc(0);
    socket.on('data', (data) => {
      if (!serverReceivedHeader) {
        headerBuf = Buffer.concat([headerBuf, data]);
        if (headerBuf.length >= 16) {
          serverReceivedHeader = {
            magic: headerBuf.readUInt32LE(0),
            mode: headerBuf.readUInt8(4),
            channels: headerBuf.readUInt8(5),
            volume: headerBuf.readUInt8(6),
            sampleRate: headerBuf.readUInt32LE(8),
          };
          resolveHeaderReceived();

          // Stream 2000 samples of 16kHz mono audio (4000 bytes)
          const chunk = Buffer.alloc(4000);
          for (let i = 0; i < 2000; i++) {
            chunk.writeInt16LE(1234, i * 2);
          }
          socket.write(chunk);
        }
      } else {
        // Stop signal received (client sent 1 byte stop)
        socket.end();
      }
    });
  });

  await new Promise((resolve) => mockServer.listen(0, '127.0.0.1', resolve));
  const port = mockServer.address().port;

  const prevIp = process.env.MK20_IP;
  process.env.MK20_IP = '127.0.0.1';
  const transport = new AudioTransport(`127.0.0.1:${port}`, undefined, port);

  try {
    // Start recording
    await transport.startDeviceRecording();
    await headerReceivedPromise;

    // Verify SNAU header arrived at mock server
    assert.ok(serverReceivedHeader);
    assert.equal(serverReceivedHeader.magic, 0x55414e53); // 'SNAU'
    assert.equal(serverReceivedHeader.mode, 2); // MODE_RECORD
    assert.equal(serverReceivedHeader.sampleRate, 16000);

    // Stop recording
    await transport.stopDeviceRecording();

    // Pull WAV
    const wavPath = await transport.pullDeviceWav();
    assert.ok(fs.existsSync(wavPath));

    const wavBuf = fs.readFileSync(wavPath);
    assert.equal(wavBuf.toString('utf-8', 0, 4), 'RIFF');
    assert.equal(wavBuf.readUInt16LE(22), 1); // 1 ch
    assert.equal(wavBuf.readUInt32LE(24), 16000); // 16kHz
    assert.equal(wavBuf.readUInt32LE(40), 4000); // 4000 bytes PCM

    // Clean up
    fs.rmSync(wavPath, { force: true });
  } finally {
    if (prevIp !== undefined) process.env.MK20_IP = prevIp;
    else delete process.env.MK20_IP;
    mockServer.close();
  }
});
