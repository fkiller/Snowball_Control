#!/usr/bin/env node
// Read-only multi-repository checkout inventory. This is not a release lockfile.
import { spawnSync } from 'node:child_process';
import { readFileSync, realpathSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const repositories = Object.freeze([
  'Snowball_Control',
  'Snowball_Device_M5Stack',
  'Snowball_Harness_Antigravity',
  'Snowball_Harness_Codex',
  'Snowball_Harness_OpenCode',
  'Snowball_Middleware',
]);

const defaultRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const gitEnv = { ...process.env, GIT_OPTIONAL_LOCKS: '0' };
// Do not let an invoking shell redirect the inventory to an unrelated repository.
for (const name of ['GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE', 'GIT_COMMON_DIR']) delete gitEnv[name];

function git(directory, args) {
  const result = spawnSync('git', ['-C', directory, ...args], {
    encoding: 'utf8', env: gitEnv, maxBuffer: 1024 * 1024, windowsHide: true,
  });
  if (result.error || result.status !== 0) throw new Error('Git checkout could not be inspected');
  return result.stdout.trim();
}

export function inspectWorkspace(root, expectedBranch) {
  return repositories.map(name => {
    try {
      const directory = path.join(root, name);
      const top = git(directory, ['rev-parse', '--show-toplevel']);
      if (realpathSync(top) !== realpathSync(directory)) throw new Error('Not a repository root');
      const head = git(directory, ['rev-parse', '--verify', 'HEAD^{commit}']);
      if (!/^(?:[0-9a-f]{40}|[0-9a-f]{64})$/.test(head)) throw new Error('Invalid HEAD');
      const branch = git(directory, ['branch', '--show-current']) || null;
      const dirty = Boolean(git(directory, ['status', '--porcelain=v1', '--untracked-files=normal', '--ignore-submodules=none']));
      return { name, branch, head, dirty, branchMatches: expectedBranch === undefined ? null : branch === expectedBranch };
    } catch {
      // Never print git stderr, filenames, or remote URLs: they can contain secrets.
      return { name, branch: null, head: null, dirty: null, branchMatches: expectedBranch === undefined ? null : false, error: 'checkout_unavailable' };
    }
  });
}

function parseArguments(args) {
  const mode = args.shift();
  if (!['inspect', 'lock', 'verify'].includes(mode)) throw new Error('Choose inspect, lock, or verify');
  const options = { mode, root: defaultRoot };
  const seen = new Set();
  while (args.length) {
    const flag = args.shift();
    if (!['--root', '--branch', '--manifest'].includes(flag) || !args.length) throw new Error('Unknown or incomplete option');
    const key = flag.slice(2);
    if (seen.has(key)) throw new Error('Duplicate option');
    seen.add(key);
    options[key] = args.shift();
    if (!options[key] || options[key].startsWith('--')) throw new Error('Unknown or incomplete option');
  }
  if (mode === 'verify' && !options.manifest) throw new Error('verify requires --manifest');
  if (mode !== 'verify' && options.manifest) throw new Error('--manifest is only valid for verify');
  return options;
}

function validateManifest(value) {
  if (!value || value.version !== 1 || !Array.isArray(value.repositories) || value.repositories.length !== repositories.length) throw new Error('Invalid manifest');
  return repositories.map((name, index) => {
    const item = value.repositories[index];
    if (!item || item.name !== name || (item.branch !== null && (typeof item.branch !== 'string' || !item.branch)) || !/^(?:[0-9a-f]{40}|[0-9a-f]{64})$/.test(item.head ?? '')) throw new Error('Invalid manifest');
    return item;
  });
}

function main() {
  const options = parseArguments(process.argv.slice(2));
  const current = inspectWorkspace(path.resolve(options.root), options.branch);
  const valid = current.every(item => item.head && !item.dirty && item.branchMatches !== false);
  if (options.mode === 'inspect') {
    process.stdout.write(`${JSON.stringify({ version: 1, repositories: current }, null, 2)}\n`);
    if (!valid) process.exitCode = 1;
    return;
  }
  if (!valid) throw new Error('One or more checkouts are unavailable, dirty, or on a different branch; run inspect for details');
  if (options.mode === 'lock') {
    process.stdout.write(`${JSON.stringify({ version: 1, repositories: current.map(({ name, branch, head }) => ({ name, branch, head })) }, null, 2)}\n`);
    return;
  }
  let manifest;
  try { manifest = JSON.parse(readFileSync(options.manifest, 'utf8')); }
  catch { throw new Error('Manifest could not be read or parsed'); }
  const locked = validateManifest(manifest);
  const mismatch = current.filter((item, i) => item.head !== locked[i].head || item.branch !== locked[i].branch).map(item => item.name);
  if (mismatch.length) throw new Error(`Manifest mismatch: ${mismatch.join(', ')}`);
  process.stdout.write('All checkout revisions and branches match the manifest and have no tracked or untracked changes.\n');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) {
    // Do not expose arbitrary filesystem or parser errors that may contain paths or input contents.
    const message = error instanceof SyntaxError ? 'Invalid manifest' : error.message;
    process.stderr.write(`workspace-revisions: ${message}\n`);
    process.exitCode = 1;
  }
}
