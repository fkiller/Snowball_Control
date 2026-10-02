import { test } from 'node:test';
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import { UdpTransport } from '../dist/transport/udp.js';
import { ContextManager } from '../dist/state/context.js';
const bind = socket => new Promise(resolve => socket.bind(0,'127.0.0.1',resolve));

test('UDP accepts valid input only from the configured endpoint and never retargets on malformed packets', async () => {
  const device=dgram.createSocket('udp4'), stranger=dgram.createSocket('udp4');
  await bind(device); await bind(stranger);
  const transport=new UdpTransport(0,'127.0.0.1',device.address().port);
  await transport.start();
  const accepted=[]; transport.on('device_input',packet=>accepted.push(packet));
  const port=transport.socket.address().port;
  const send=(socket,value)=>new Promise((resolve,reject)=>socket.send(Buffer.from(value),port,'127.0.0.1',error=>error?reject(error):resolve()));
  try {
    await send(stranger,'{"type":"key","keyId":16,"isDown":true}');
    for (const value of ['null','[]','{','{"type":"key","keyId":99,"isDown":true}','{"type":"key","keyId":16}']) await send(device,value);
    await send(device,'{"type":"key","keyId":16,"isDown":true}');
    await new Promise(resolve=>setTimeout(resolve,50));
    assert.deepEqual(accepted,[{type:'key',keyId:16,isDown:true}]);
    const reply=new Promise(resolve=>device.once('message',msg=>resolve(JSON.parse(msg))));
    transport.sendSync(new ContextManager());
    assert.equal((await reply).type,'v2_sync');
  } finally { await transport.stop(); device.close(); stranger.close(); }
});
