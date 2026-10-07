import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AudioTransport } from '../dist/audio/transport.js';
import { AudioPlayer } from '../dist/audio/player.js';
import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

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

test('TCP connect without codec readiness and explicit rejection never start capture or fall back to ADB', async t => {
  for(const response of [null,'ERR1']){
    const server=net.createServer(socket=>{socket.on('error',()=>{});socket.once('data',()=>socket.end(response||undefined));});
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    const capture=new AudioTransport('127.0.0.1','missing-maintenance-only-adb',server.address().port,'a'.repeat(32));
    await assert.rejects(capture.startDeviceRecording(), /before readiness|rejected/);
    await new Promise(resolve=>server.close(resolve));
  }
});

test('playback is bound to the selected lease and requires real daemon completion', async t => {
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'snowball-audio-test-'));
  t.after(()=>fs.rmSync(dir,{recursive:true,force:true}));
  const wav=path.join(dir,'fixture.wav');fs.writeFileSync(wav,AudioTransport.monoPcmToWav(Buffer.alloc(3200)));
  for(const completion of ['DONE','ERR1']){
    let bytes=Buffer.alloc(0),ready=false;
    const server=net.createServer({allowHalfOpen:true},socket=>{
      socket.on('error',()=>{});
      socket.on('data',chunk=>{
        bytes=Buffer.concat([bytes,chunk]);
        if(!ready&&bytes.length>=48){ready=true;socket.write('RDY1');}
      });
      socket.on('end',()=>socket.end(completion));
    });
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    const player=new AudioPlayer('127.0.0.1',undefined,'a'.repeat(32),server.address().port);
    if(completion==='DONE')await player.playOnDevice(wav);else await assert.rejects(player.playOnDevice(wav),/failed|interrupted/);
    assert.equal(bytes[4],0x81);assert.equal(bytes.subarray(16,48).toString(),'a'.repeat(32));
    assert.equal(bytes.length,48+3200);
    await new Promise(resolve=>server.close(resolve));
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
          socket.write(Buffer.concat([Buffer.from("RDY1"),chunk]));
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

test('cancel fences microphone readiness arriving after cancellation',async t=>{
  const sockets=new Set();
  const server=net.createServer(socket=>{sockets.add(socket);socket.on('error',()=>{});socket.once('data',()=>setTimeout(()=>{if(!socket.destroyed)socket.write('RDY1');},50));socket.on('close',()=>sockets.delete(socket));});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  t.after(async()=>{for(const socket of sockets)socket.destroy();await new Promise(resolve=>server.close(resolve));});
  const transport=new AudioTransport('127.0.0.1',undefined,server.address().port,'a'.repeat(32));
  const start=transport.startDeviceRecording();
  await transport.cancelDeviceRecording();
  await assert.rejects(start,/cancelled before readiness/);
  await assert.rejects(transport.pullDeviceWav(),/insufficient/);
});
