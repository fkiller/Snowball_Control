import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { repositories } from '../workspace-revisions.mjs';

const script = fileURLToPath(new URL('../workspace-revisions.mjs', import.meta.url));
function command(bin, args, cwd) {
  const result = spawnSync(bin, args, { cwd, encoding: 'utf8', env: { ...process.env, GIT_CONFIG_NOSYSTEM: '1' } });
  if (result.error) throw result.error;
  return result;
}
function git(directory, ...args) {
  const result = command('git', ['-C', directory, ...args]);
  assert.equal(result.status, 0, result.stderr);
  return result.stdout.trim();
}
function workspace(t) {
  const root = mkdtempSync(path.join(os.tmpdir(), 'snowball-revisions-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  for (const name of repositories) {
    const directory = path.join(root, name);
    mkdirSync(directory);
    git(directory, 'init', '--quiet');
    git(directory, 'config', 'user.name', 'Test');
    git(directory, 'config', 'user.email', 'test@example.invalid');
    git(directory, 'switch', '--quiet', '-c', 'feat/remote-expansion');
    writeFileSync(path.join(directory, 'tracked'), name);
    git(directory, 'add', 'tracked');
    git(directory, 'commit', '--quiet', '-m', 'initial');
  }
  return root;
}
function run(root, ...args) { return command(process.execPath, [script, ...args, '--root', root]); }

test('locks and verifies clean independent repositories, detects changed branch and HEAD', t => {
  const root = workspace(t);
  const locked = run(root, 'lock', '--branch', 'feat/remote-expansion');
  assert.equal(locked.status, 0, locked.stderr);
  const manifest = JSON.parse(locked.stdout);
  assert.deepEqual(manifest.repositories.map(r => r.name), repositories);
  assert.ok(manifest.repositories.every(r => r.branch === 'feat/remote-expansion' && /^[a-f0-9]{40}$/.test(r.head)));
  assert.equal(locked.stdout.includes(root), false);
  const file = path.join(root, 'revisions.json');
  writeFileSync(file, locked.stdout);
  assert.equal(run(root, 'verify', '--manifest', file).status, 0);

  const target = path.join(root, repositories[0]);
  git(target, 'switch', '--quiet', '-c', 'different-branch');
  assert.notEqual(run(root, 'verify', '--manifest', file).status, 0);
  git(target, 'switch', '--quiet', 'feat/remote-expansion');
  writeFileSync(path.join(target, 'tracked'), 'new revision');
  git(target, 'commit', '--quiet', '-am', 'new revision');
  assert.match(run(root, 'verify', '--manifest', file).stderr, /Manifest mismatch/);
});

test('dirty, missing, detached, and wrong-branch checkouts never yield a lock', t => {
  const root = workspace(t);
  const target = path.join(root, repositories[1]);
  writeFileSync(path.join(target, 'untracked-secret-name'), 'secret value');
  const inspection = run(root, 'inspect', '--branch', 'feat/remote-expansion');
  assert.equal(inspection.status, 1);
  assert.equal(JSON.parse(inspection.stdout).repositories[1].dirty, true);
  assert.doesNotMatch(inspection.stdout + inspection.stderr, /secret/);
  const dirtyLock = run(root, 'lock', '--branch', 'feat/remote-expansion');
  assert.equal(dirtyLock.status, 1);
  assert.equal(dirtyLock.stdout, '');
  rmSync(path.join(target, 'untracked-secret-name'));
  git(target, 'checkout', '--quiet', '--detach');
  assert.equal(run(root, 'lock', '--branch', 'feat/remote-expansion').status, 1);
  const detached = JSON.parse(run(root, 'inspect').stdout);
  assert.equal(detached.repositories[1].branch, null);
  rmSync(target, { recursive: true, force: true });
  const missing = run(root, 'inspect');
  assert.equal(missing.status, 1);
  assert.equal(JSON.parse(missing.stdout).repositories[1].error, 'checkout_unavailable');
});
