// Explicit, disposable live acceptance probe. Not part of npm test.
import { mkdtemp, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { CodexAdapter } from '../dist/codex/adapter.js';
const adapter = new CodexAdapter();
const result = { startedAt: new Date().toISOString(), passed: false };
let deadline;
try {
  await adapter.start();
  const models = await adapter.request('model/list', {});
  result.modelCount = models.data?.length;
  result.cwd = await mkdtemp(path.join(tmpdir(), 'snowball-mvp-'));
  const response = await adapter.request('thread/start', { cwd: result.cwd, approvalPolicy: 'on-request' });
  result.threadId = response.thread.id;
  const completed = new Promise((resolve, reject) => {
    deadline = setTimeout(() => reject(new Error('No completed event after 45 seconds')), 45000);
    adapter.on('turn_completed', event => { if (event.threadId === result.threadId) resolve(event); });
  });
  await adapter.request('turn/start', { threadId: result.threadId, input: [{ type:'text', text:'This is a disposable Snowball integration check. Do not use tools or change files. Reply exactly: 한글 TOP 표시 확인 완료' }] });
  const event = await completed;
  result.status = event.turn.status;
  const read = await adapter.request('thread/read', { threadId:result.threadId, includeTurns:true });
  result.text = read.thread.turns.flatMap(t=>t.items).filter(i=>i.type==='agentMessage').map(i=>i.text).join('\n');
  result.passed = result.status === 'completed' && result.text.includes('한글 TOP 표시 확인 완료');
} catch(error) { result.error = String(error); }
finally {
  clearTimeout(deadline);
  await adapter.stop();
  await writeFile(new URL('../../docs/mvp-live-result.json', import.meta.url), JSON.stringify(result,null,2));
  console.log(JSON.stringify(result));
  if (!result.passed) process.exitCode = 1;
}
