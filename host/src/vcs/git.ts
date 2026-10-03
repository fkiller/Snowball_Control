import * as cp from "node:child_process";
import * as syncFs from "node:fs";
import * as path from "node:path";

export interface ChangedFile { path: string; name: string; status: string; additions: number; deletions: number; }

export class GitProvider {
  static async getChangedFiles(cwd = process.cwd()): Promise<ChangedFile[]> {
    return new Promise(resolve => {
      cp.execFile('git', ['status', '--porcelain=v1', '-z', '-uall'],
        { cwd, windowsHide: true, maxBuffer: 1024 * 1024, timeout: 10000 }, (err, stdout) => {
        if (err) return resolve([]);
        const entries = stdout.split('\0');
        const files: ChangedFile[] = [];
        for (let i = 0; i < entries.length; i++) {
          const entry = entries[i];
          if (entry.length < 4) continue;
          const status = entry.slice(0, 2);
          const filePath = entry.slice(3);
          if (/[RC]/.test(status)) i++; // -z emits destination then source.
          files.push({ path: filePath, name: path.basename(filePath), status: status.trim(), additions: 0, deletions: 0 });
        }
        resolve(files);
      });
    });
  }

  static async getFileDiff(filePath: string, cwd = process.cwd()): Promise<string[]> {
    const root = path.resolve(cwd);
    const fullPath = path.resolve(root, filePath);
    const within = (target: string) => {
      const rel = path.relative(root, target);
      return rel !== '..' && !rel.startsWith('..' + path.sep) && !path.isAbsolute(rel);
    };
    if (typeof filePath !== 'string' || filePath.includes('\0') || !within(fullPath)) return ['File is outside the selected project'];
    try { if (!within(syncFs.realpathSync(fullPath))) return ['File is outside the selected project']; } catch {}
    return new Promise(resolve => {
      const options = { cwd: root, windowsHide: true, maxBuffer: 1024 * 1024, timeout: 10000 };
      const lines = (output: string) => output.split('\n').map(line => line.replace(/\r$/, ''));
      cp.execFile('git', ['--literal-pathspecs', 'diff', '--no-ext-diff', '--no-textconv', 'HEAD', '--', filePath], options, (err, stdout) => {
        if (!err && stdout.trim()) return resolve(lines(stdout));
        cp.execFile('git', ['diff', '--no-index', '--no-ext-diff', '--no-textconv', '--', process.platform === 'win32' ? 'NUL' : '/dev/null', fullPath], options, (_err, output) => {
          if (output.trim()) return resolve(lines(output));
          resolve([`No changes detected for ${filePath}`]);
        });
      });
    });
  }
}
