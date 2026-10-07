import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// Build hardware/mk20/hud with the vendor Tina toolchain using its Makefile first.
// This script packages real build output; it never substitutes a downloaded/device binary.
const [destination, fontFile, fontLicense] = process.argv.slice(2);
if (!destination || !fontFile || !fontLicense) throw Error('Usage: package-runtime.mjs EMPTY_STAGE_DIRECTORY D2Coding.ttf OFL.txt');
const root = fileURLToPath(new URL('../../..', import.meta.url));
const stage = path.resolve(destination);
if(execFileSync('git',['status','--porcelain','--untracked-files=normal','--','hardware/mk20/hud','hardware/mk20/dev-tools/lunch.sh','hardware/mk20/qmk'],{cwd:root,encoding:'utf8'}).trim())throw Error('Commit runtime source before packaging');
if (fs.existsSync(stage)) throw Error('Stage directory must be new');
fs.mkdirSync(stage, { recursive: true });
const deploy = ['mk20-hud', 'mk20-audio', 'lunch.sh', 'fonts/D2Coding.ttf'];
const copy = (source, name) => { fs.mkdirSync(path.dirname(path.join(stage, name)), { recursive: true }); fs.copyFileSync(source, path.join(stage, name)); };
for (const name of ['mk20-hud', 'mk20-audio']) copy(path.join(root, 'hardware/mk20/hud', name), name);
copy(path.join(root,'hardware/mk20/dev-tools/lunch.sh'), 'lunch.sh');
copy(fontFile, 'fonts/D2Coding.ttf'); copy(fontLicense, 'licenses/D2Coding-OFL.txt');
copy(path.join(root, 'LICENSE'), 'licenses/Snowball-LICENSE.txt');
copy(path.join(root, 'hardware/mk20/qmk/LICENSE'), 'licenses/QMK-LICENSE.txt');
copy(path.join(root, 'hardware/mk20/qmk/bin/syk_keyboards_mk20_plus_via.bin'), 'qmk/syk_keyboards_mk20_plus_via.bin');
for(const name of execFileSync('git',['ls-files','hardware/mk20/qmk'],{cwd:root,encoding:'utf8'}).trim().split('\n')){
  if(!name||name.includes('/bin/'))continue;
  copy(path.join(root,name),'source/qmk/'+name.slice('hardware/mk20/qmk/'.length));
}
for (const name of fs.readdirSync(path.join(root, 'hardware/mk20/hud')).filter(n => /\.(c|h)$/.test(n) || n === 'Makefile')) copy(path.join(root, 'hardware/mk20/hud', name), 'source/hud/' + name);
const files = [];
function scan(directory, prefix = '') {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const name = prefix + entry.name, full = path.join(directory, entry.name);
    if (entry.isDirectory()) scan(full, name + '/');
    else files.push({ path: name, sha256: createHash('sha256').update(fs.readFileSync(full)).digest('hex'), bytes: fs.statSync(full).size });
  }
}
scan(stage);
const commit = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim();
fs.writeFileSync(path.join(stage, 'manifest.json'), JSON.stringify({ version: 1, deviceProtocol:'SNMK1', audioProtocol:'SNAU-lease-v1', sourceCommit: commit, target: 'Tina Linux ARMv7 hard-float', toolchain: 'vendor arm-openwrt-linux GCC 6.4.1 / glibc 2.23; FreeType 2.6.1 headers', deploy, files }, null, 2) + '\n');
console.log(stage);
