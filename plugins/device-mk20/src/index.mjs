import dgram from 'node:dgram';
import { isIPv4 } from 'node:net';
import { EventEmitter } from 'node:events';

export class Mk20Fault extends Error { constructor(code) { super(code); this.code = code; } }
const requireThat = (value, code) => { if (!value) throw new Mk20Fault(code); };
const int = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
const local = address => isIPv4(address) && (address.startsWith('127.') || address.startsWith('10.') || address.startsWith('192.168.') || address.startsWith('169.254.') || (address.startsWith('172.') && Number(address.split('.')[1]) >= 16 && Number(address.split('.')[1]) <= 31));
export const compatibility = () => ({ model: 'MK20', protocol: 'legacy-udp-v2', enrollment: 'unpaired', connectivity: 'lab_only', controllable: false, authenticated: false, canMergeUsbLan: false, reason: 'firmware_has_no_authenticated_pairing' });
export function requireProductionControl() { throw new Mk20Fault('authenticated_firmware_required'); }
export function decodeLegacyInput(bytes) {
  requireThat(Buffer.isBuffer(bytes) && bytes.length > 0 && bytes.length <= 1024, 'invalid_packet');
  let value; try { value = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)); } catch { throw new Mk20Fault('invalid_packet'); }
  requireThat(value && typeof value === 'object' && !Array.isArray(value), 'invalid_packet');
  requireThat(value.seq === undefined || int(value.seq,0,Number.MAX_SAFE_INTEGER), 'invalid_sequence');
  const sequence = value.seq === undefined ? {} : { sequence: value.seq };
  if (value.type === 'key') {
    requireThat(Object.keys(value).every(k=>['type','keyId','isDown','seq'].includes(k)) && int(value.keyId,1,20) && typeof value.isDown === 'boolean', 'invalid_key');
    return { kind:'button',button:`key-${value.keyId}`,pressed:value.isDown,trust:'untrusted_lab',...sequence };
  }
  if (value.type === 'knob_left' || value.type === 'knob_right') {
    requireThat(Object.keys(value).every(k=>['type','delta','isClick','seq'].includes(k)), 'invalid_knob');
    const knob = value.type === 'knob_left' ? 'left' : 'right';
    if (value.isClick === true && value.delta === undefined) return {kind:'knob-click',knob,trust:'untrusted_lab',...sequence};
    requireThat(value.isClick === undefined && [-1,1].includes(value.delta), 'invalid_knob');
    return {kind:'knob-turn',knob,delta:value.delta,trust:'untrusted_lab',...sequence};
  }
  requireThat(value.type === 'ping' && Object.keys(value).every(k=>['type','seq'].includes(k)), 'unsupported_packet');
  return {kind:'presence',trust:'untrusted_lab',...sequence};
}
// Firmware uses substring parsing; do not emit delimiters that can masquerade as JSON fields.
function text(value, maxBytes) {
  requireThat(typeof value === 'string', 'invalid_text'); let out='';
  for (const char of value.replace(/[\x00-\x1f\x7f"\\{}\[\]]/g,' ')) { if (Buffer.byteLength(out+char)>maxBytes) break; out+=char; }
  return out;
}
/** Pure renderer. Deliberately no machine/harness/session or mutable global selection dependency. */
export function encodeLegacyPreview(view, sequence) {
  requireThat(view && int(sequence,0,Number.MAX_SAFE_INTEGER) && Array.isArray(view.keys) && view.keys.length <= 20 && Array.isArray(view.lines) && view.lines.length <= 8, 'invalid_view');
  requireThat(int(view.scroll,0,1_000_000) && int(view.totalLines,0,1_000_000) && int(view.volume,0,100) && typeof view.muted === 'boolean', 'invalid_view');
  const ids = new Set(); const keys = view.keys.map(key=>{
    requireThat(int(key.id,1,20) && !ids.has(key.id) && int(key.flags ?? 0,0,63), 'invalid_key');ids.add(key.id);
    return {id:key.id,top:text(key.top ?? '',15),main:text(key.main ?? '',23),sub:text(key.sub ?? '',23),flags:key.flags ?? 0};
  });
  const packet={type:'v2_sync',seq:sequence,viewMode:text(view.mode ?? 'lab',15),topTitle:text(view.title,63),topSubtitle:text(view.subtitle ?? '',63),topBody:view.lines.map(line=>text(line,168)).join('\n'),topScroll:view.scroll,topTotalLines:view.totalLines,volume:view.volume,isMuted:view.muted,keys};
  const encoded=Buffer.from(JSON.stringify(packet)); requireThat(encoded.length <= 1400, 'datagram_capacity'); return encoded;
}

/** Explicit lab-only transport. It cannot grant core Controller permissions. */
export class Mk20LabTransport extends EventEmitter {
  #socket; #starting=false; #generation=0; #sequence=0; #lastSequence=-1; #window=Date.now(); #count=0;
  constructor({labEnabled=false,localAddress,targetAddress,targetPort=7701,localPort=0}) {
    super();requireThat(labEnabled === true,'lab_opt_in_required');
    requireThat(local(localAddress)&&local(targetAddress)&&int(targetPort,1,65535)&&int(localPort,0,65535),'invalid_endpoint');
    this.endpoint=Object.freeze({localAddress,targetAddress,targetPort,localPort});
  }
  async start() {
    requireThat(!this.#socket&&!this.#starting,'already_started');this.#starting=true;const generation=++this.#generation;
    const socket=dgram.createSocket('udp4');this.#socket=socket;
    socket.on('message',(bytes,remote)=>{
      if(this.#socket!==socket||generation!==this.#generation||remote.address!==this.endpoint.targetAddress||remote.port!==this.endpoint.targetPort)return;
      if(Date.now()-this.#window>=1000){this.#window=Date.now();this.#count=0;}if(++this.#count>100)return;
      try {const input=decodeLegacyInput(bytes);if(input.sequence!==undefined){if(input.sequence<=this.#lastSequence)return;this.#lastSequence=input.sequence;}
        this.emit('lab.input',input);
      }catch{/* Invalid/untrusted input never becomes a command. */}
    });
    socket.on('error',()=>{this.emit('lab.status',{...compatibility(),reason:'transport_error'});void this.close();});
    try {
      await new Promise((resolve,reject)=>{socket.once('error',reject);socket.once('close',()=>reject(new Mk20Fault('closed')));socket.bind(this.endpoint.localPort,this.endpoint.localAddress,()=>{socket.off('error',reject);resolve();});});
      requireThat(generation===this.#generation,'closed');return {address:socket.address(),...compatibility()};
    }finally{this.#starting=false;}
  }
  async preview(view) {
    const socket=this.#socket;requireThat(socket&&!this.#starting,'not_running');
    const bytes=encodeLegacyPreview(view,++this.#sequence);
    await new Promise((resolve,reject)=>socket.send(bytes,this.endpoint.targetPort,this.endpoint.targetAddress,error=>error?reject(new Mk20Fault('preview_failed')):resolve()));
    return {sentBytes:bytes.length,delivery:'unacknowledged_lab'};
  }
  async close() {++this.#generation;const socket=this.#socket;this.#socket=undefined;this.#lastSequence=-1;if(socket)await new Promise(resolve=>{try{socket.close(resolve);}catch{resolve();}});}
}
