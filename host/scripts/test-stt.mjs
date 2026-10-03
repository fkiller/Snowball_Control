import { spawnSync } from 'node:child_process';
const result = spawnSync(process.execPath, ['--test', 'tests/integration.test.mjs', 'tests/whisper-worker.test.mjs'], {
  stdio: 'inherit', env: { ...process.env, SNOWBALL_TEST_STT: '1' }, windowsHide: true,
});
if (result.error) throw result.error;
process.exitCode = result.status ?? 1;
