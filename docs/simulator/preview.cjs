// No dependencies. Serve the canonical fragment, or export a portable page.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const page = () => '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Snowball controls revision 5</title><style>:root{color-scheme:light dark}body{margin:16px}</style></head><body>' + fs.readFileSync(path.join(__dirname, 'controls.html'), 'utf8') + '</body></html>';
if (process.argv.includes('--export')) {
  fs.writeFileSync(path.join(__dirname, 'index.html'), page());
  console.log('Exported docs/simulator/index.html');
} else {
  http.createServer((req, res) => {
    if (req.url !== '/') { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(page());
  }).listen(8768, '127.0.0.1', () => console.log('http://127.0.0.1:8768'));
}

