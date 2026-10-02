import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
const directory=path.resolve(process.argv[2] || 'tests');
const files=fs.readdirSync(directory).filter(name=>name.endsWith('.test.mjs')).sort().map(name=>path.join(directory,name));
if (!files.length) throw new Error('No test files found');
const result=spawnSync(process.execPath,['--test',...files],{stdio:'inherit',windowsHide:true});
if(result.error) throw result.error;
process.exitCode=result.status ?? 1;
