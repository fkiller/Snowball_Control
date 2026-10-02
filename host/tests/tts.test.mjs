import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { MvpController } from '../dist/state/mvp-controller.js';

class MockBackend extends EventEmitter {
  constructor() {
    super();
    this.requests = [];
  }
  async start() {}
  async stop() {}
  async request(method, params) {
    this.requests.push({ method, params });
    if (method === 'thread/list') {
      return {
        data: [
          { id: 'session-1', cwd: process.cwd(), name: 'Test Session', preview: 'Preview 1' },
        ],
      };
    }
    if (method === 'thread/read') {
      return {
        thread: {
          id: 'session-1',
          turns: [
            {
              id: 'turn-1',
              status: 'completed',
              items: [
                { type: 'userMessage', text: 'Hello AI' },
                { type: 'agentMessage', text: 'Hello! I am your AI assistant.' },
              ],
            },
          ],
        },
      };
    }
    if (method === 'model/list') {
      return { data: [{ id: 'gpt-4o', model: 'gpt-4o', isDefault: true }] };
    }
    return {};
  }
}

class MockVoiceProvider {
  constructor() {
    this.started = [];
    this.finished = [];
    this.cancelled = [];
  }
  async start(id) { this.started.push(id); }
  async finish(id) { this.finished.push(id); return 'Next prompt from user'; }
  async cancel(id) { this.cancelled.push(id); }
  close() {}
}

class MockTtsProvider {
  constructor() {
    this.spokenTexts = [];
    this.stopped = false;
  }
  async synthesize(text, options) {
    this.spokenTexts.push({ text, options });
    return { wavPath: '/tmp/test.wav', durationMs: 500, sampleRate: 24000, text, engine: 'mock' };
  }
  async speak(text, options) {
    this.spokenTexts.push({ text, options });
  }
  async stop() {
    this.stopped = true;
  }
  close() {}
}

const mockDesktop = { isConnected: false, connect: async () => false, disconnect: () => {}, sendMessageToThread: async () => ({}) };

test('TTS Toggle: Speak button (Key 12) toggles Auto-TTS ON, immediately speaks latest turn LLM response', async () => {
  const backend = new MockBackend();
  const voice = new MockVoiceProvider();
  const tts = new MockTtsProvider();
  const controller = new MvpController(backend, voice, () => {}, undefined, mockDesktop, tts);

  await controller.connect();

  // Initially Auto-TTS is off
  assert.equal(controller.context.autoTts, false);
  assert.equal(controller.context.isSpeaking, false);
  let k12 = controller.state().keys.find(k => k.keyId === 12);
  assert.equal(k12?.labelMain, 'Speak');
  assert.equal(k12?.labelSub, 'Off');
  assert.equal(k12?.isDisabled, false);

  // Press Key 12 to Toggle ON
  await controller.toggleAutoTts();

  // State is now Auto-TTS ON
  assert.equal(controller.context.autoTts, true);
  k12 = controller.state().keys.find(k => k.keyId === 12);
  assert.equal(k12?.labelTop, 'AUTO TTS');
  assert.equal(k12?.labelMain, 'Speak');
  assert.equal(k12?.labelSub, 'Auto ON');
  assert.equal(k12?.isFilled, true);

  // Immediately spoke the latest turn LLM response ("Hello! I am your AI assistant.")
  assert.equal(tts.spokenTexts.length, 1);
  assert.equal(tts.spokenTexts[0].text, 'Hello! I am your AI assistant.');
  await controller.close();
});

test('TTS Toggle: Pressing Speak (Key 12) again toggles OFF and immediately stops speech', async () => {
  const backend = new MockBackend();
  const voice = new MockVoiceProvider();
  const tts = new MockTtsProvider();
  const controller = new MvpController(backend, voice, () => {}, undefined, mockDesktop, tts);

  await controller.connect();
  await controller.toggleAutoTts(); // Turn ON
  assert.equal(controller.context.autoTts, true);

  // Turn OFF
  tts.stopped = false;
  await controller.toggleAutoTts(); // Turn OFF

  assert.equal(controller.context.autoTts, false);
  const k12 = controller.state().keys.find(k => k.keyId === 12);
  assert.equal(k12?.labelSub, 'Off');
  assert.equal(k12?.isFilled, false);
  assert.equal(tts.stopped, true);
  await controller.close();
});

test('TTS Auto Sequence: Subsequent completed turn automatically speaks LLM response without pressing Speak', async () => {
  const backend = new MockBackend();
  const voice = new MockVoiceProvider();
  const tts = new MockTtsProvider();
  const controller = new MvpController(backend, voice, () => {}, undefined, mockDesktop, tts);

  await controller.connect();
  await controller.toggleAutoTts(); // Turn ON (speaks turn-1)
  assert.equal(tts.spokenTexts.length, 1);

  // Simulate arrival of a new turn from LLM
  backend.request = async (method, params) => {
    if (method === 'thread/read') {
      return {
        thread: {
          id: 'session-1',
          turns: [
            {
              id: 'turn-1',
              status: 'completed',
              items: [
                { type: 'userMessage', text: 'Hello AI' },
                { type: 'agentMessage', text: 'Hello! I am your AI assistant.' },
              ],
            },
            {
              id: 'turn-2',
              status: 'completed',
              items: [
                { type: 'userMessage', text: 'What is 2+2?' },
                { type: 'agentMessage', text: '2 + 2 equals 4.' },
              ],
            },
          ],
        },
      };
    }
    return {};
  };

  // Simulate turn/completed event from backend
  backend.emit('raw_event', {
    method: 'turn/completed',
    params: {
      threadId: 'session-1',
      turn: { id: 'turn-2', status: 'completed' },
    },
  });

  // Wait for async load and speak to trigger
  await new Promise(r => setTimeout(r, 50));

  // Should have automatically spoken turn-2!
  assert.equal(tts.spokenTexts.length, 2);
  assert.equal(tts.spokenTexts[1].text, '2 + 2 equals 4.');
  await controller.close();
});

test('TTS Interruption: Starting voice input (Talk / Key 20) stops active speech immediately', async () => {
  const backend = new MockBackend();
  const voice = new MockVoiceProvider();
  const tts = new MockTtsProvider();
  const controller = new MvpController(backend, voice, () => {}, undefined, mockDesktop, tts);

  await controller.connect();
  await controller.toggleAutoTts(); // Turn ON
  controller.context.isSpeaking = true;
  tts.stopped = false;

  // Dispatch Key 20 (Talk)
  const packet = { type: 'key', keyId: 20, isDown: true, trust: 'untrusted_lab' };
  // Access private dispatch through prototype or onButton
  await (controller).dispatch(packet);

  // Speech should be stopped so microphone doesn't pick up speaker audio
  assert.equal(tts.stopped, true);
  // But autoTts remains ON for the next turn
  assert.equal(controller.context.autoTts, true);
  await controller.close();
});

test('TTS Fallback: Speeks earlier turn if the latest turn is failed or has no agent response', async () => {
  const backend = new MockBackend();
  const voice = new MockVoiceProvider();
  const tts = new MockTtsProvider();
  const controller = new MvpController(backend, voice, () => {}, undefined, mockDesktop, tts);

  // Override thread/read with a thread where Turn 1 had an agent response, but Turn 2 was failed with no agent response
  backend.request = async (method, params) => {
    if (method === 'thread/list') {
      return {
        threads: [
          { id: 'session-failed', preview: 'Initial preview fallback' },
        ],
      };
    }
    if (method === 'thread/read') {
      return {
        thread: {
          id: 'session-failed',
          turns: [
            {
              id: 'turn-1',
              status: 'completed',
              items: [
                { type: 'userMessage', text: 'Turn 1 user request' },
                { type: 'agentMessage', text: 'Turn 1 successful agent response' },
              ],
            },
            {
              id: 'turn-2',
              status: 'failed',
              items: [
                { type: 'userMessage', text: 'Turn 2 failed request' },
              ],
            },
          ],
        },
      };
    }
    return {};
  };

  await controller.connect();
  // Turn 2 is the latest turn, but has no agent response.
  // toggleAutoTts should find Turn 1's agent response!
  await controller.toggleAutoTts();

  assert.equal(controller.context.autoTts, true);
  assert.equal(tts.spokenTexts.length, 1);
  assert.equal(tts.spokenTexts[0].text, 'Turn 1 successful agent response');
  await controller.close();
});

