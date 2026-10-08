import test from 'node:test';
import assert from 'node:assert/strict';
import {LocalWhisperProvider} from '../dist/audio/local-whisper.js';

test('closed speech providers reject readiness and cannot restart a native worker',async()=>{
  const provider=new LocalWhisperProvider();
  provider.close();
  await assert.rejects(provider.ready(),/provider is closed/);
  assert.match(await provider.status(),/unavailable.*provider is closed/);
});
