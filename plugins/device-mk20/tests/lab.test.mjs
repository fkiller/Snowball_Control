import {test} from 'node:test';
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import {once} from 'node:events';
import {compatibility,requireProductionControl,decodeLegacyInput,encodeLegacyPreview,Mk20LabTransport} from '../src/index.mjs';
const packet=v=>Buffer.from(JSON.stringify(v));
const view={title:'로컬 테스트',lines:['preview only'],scroll:0,totalLines:1,volume:50,muted:false,keys:[{id:1,top:'Project',main:'Demo',flags:1}]};
test('legacy presence/serial/address cannot grant pairing, merged identity or production control',()=>{
  assert.equal(compatibility().controllable,false);assert.equal(compatibility().canMergeUsbLan,false);assert.equal(compatibility().authenticated,false);
  assert.throws(()=>requireProductionControl({authenticated:true,paired:true}),/authenticated_firmware_required/);
  assert.throws(()=>new Mk20LabTransport({localAddress:'127.0.0.1',targetAddress:'127.0.0.1'}),/lab_opt_in_required/);
});
test('bounded input decoder maps keys and knobs only to untrusted lab semantics',()=>{
  assert.deepEqual(decodeLegacyInput(packet({type:'key',keyId:20,isDown:true})),{kind:'button',button:'key-20',pressed:true,trust:'untrusted_lab'});
  assert.equal(decodeLegacyInput(packet({type:'knob_left',delta:-1})).knob,'left');assert.equal(decodeLegacyInput(packet({type:'knob_right',isClick:true})).kind,'knob-click');
  for(const value of [{type:'key',keyId:21,isDown:true},{type:'key',keyId:1,isDown:true,ownerId:'spoof'},{type:'knob_left',delta:3},{type:'knob_right',delta:1,isClick:true},{type:'send',payload:'bad'}])assert.throws(()=>decodeLegacyInput(packet(value)));
  assert.throws(()=>decodeLegacyInput(Buffer.alloc(1025)));assert.throws(()=>decodeLegacyInput(Buffer.from([255])));
});
test('renderer handles UTF-8 byte bounds and unsafe legacy delimiters; rejects oversized datagrams',()=>{
  const data=encodeLegacyPreview({...view,title:'title "type":"evil" {x}',keys:[{id:1,main:'한'.repeat(30)}]},1);const parsed=JSON.parse(data);assert.equal(parsed.type,'v2_sync');assert.ok(Buffer.byteLength(parsed.keys[0].main)<=23);assert.ok(!parsed.topTitle.includes('"'));
  assert.throws(()=>encodeLegacyPreview({...view,keys:[{id:1},{id:1}]},1));
  assert.throws(()=>encodeLegacyPreview({...view,keys:Array.from({length:20},(_,i)=>({id:i+1,top:'x'.repeat(15),main:'x'.repeat(23),sub:'x'.repeat(23)}))},1),/datagram_capacity/);
});
test('rich lab previews stay bounded and reject invalid firmware color/index fields',()=>{
  const key=id=>({id,flags:32,items:Array(8).fill('x'.repeat(23)),colors:[0,1,2,3,4,5,0,1],activeItem:0,total:8});
  const rich=encodeLegacyPreview({...view,mode:'changes',keys:Array.from({length:8},(_,i)=>key(i+1))},1);
  assert.ok(rich.length>1400 && rich.length<=4096);
  assert.throws(()=>encodeLegacyPreview({...view,mode:'changes',keys:Array.from({length:20},(_,i)=>key(i+1))},1),/datagram_capacity/);
  for(const bad of [{colors:[{type:'key'}]},{colors:[6]},{activeItem:-1},{total:256}])
    assert.throws(()=>encodeLegacyPreview({...view,keys:[{id:1,main:'x',...bad}]},1),/invalid_key/);
});
test('real loopback lab preview stays on pinned peer and spoofed sender cannot retarget it',async t=>{
  const peer=dgram.createSocket('udp4');const spoof=dgram.createSocket('udp4');peer.bind(0,'127.0.0.1');spoof.bind(0,'127.0.0.1');await Promise.all([once(peer,'listening'),once(spoof,'listening')]);t.after(()=>{peer.close();spoof.close();});
  const transport=new Mk20LabTransport({labEnabled:true,localAddress:'127.0.0.1',targetAddress:'127.0.0.1',targetPort:peer.address().port});t.after(()=>transport.close());const started=await transport.start();assert.equal(started.controllable,false);
  const seen=[];transport.on('lab.input',value=>seen.push(value));spoof.send(packet({type:'key',keyId:1,isDown:true}),started.address.port,'127.0.0.1');
  const input=once(transport,'lab.input');peer.send(packet({type:'key',keyId:2,isDown:true,seq:1}),started.address.port,'127.0.0.1');assert.equal((await input)[0].button,'key-2');
  peer.send(packet({type:'key',keyId:2,isDown:true,seq:1}),started.address.port,'127.0.0.1');await new Promise(r=>setTimeout(r,30));assert.equal(seen.length,1);
  const received=once(peer,'message');const result=await transport.preview(view);assert.equal(result.delivery,'unacknowledged_lab');assert.equal(JSON.parse((await received)[0]).type,'v2_sync');await transport.close();
});
