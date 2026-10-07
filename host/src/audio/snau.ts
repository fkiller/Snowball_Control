import net from 'node:net';

export type AudioEndpoint = Readonly<{ host: string; port: number; lease?: string; localAddress?: string }>;
export function audioEndpoint(address: string, port = 7702, lease?: string, localAddress?: string): AudioEndpoint {
  const host = address.split(':')[0];
  if (!net.isIPv4(host) || !Number.isInteger(port) || port < 1 || port > 65535 || (lease !== undefined && !/^[a-f0-9]{32}$/.test(lease))) throw Error('Invalid MK20 audio endpoint');
  if(localAddress&&!net.isIPv4(localAddress))throw Error('Invalid MK20 audio source address');
  return Object.freeze({ host, port, lease, localAddress });
}
export function snauHeader(endpoint: AudioEndpoint, mode: number, channels = 1, volume = 100, muted = false, rate = 16000, length = 0): Buffer {
  const header = Buffer.alloc(endpoint.lease ? 48 : 16);
  header.write('SNAU'); header[4] = mode | (endpoint.lease ? 0x80 : 0);
  header[5] = channels; header[6] = Math.max(0, Math.min(100, Math.round(volume))); header[7] = muted ? 1 : 0;
  header.writeUInt32LE(rate, 8); header.writeUInt32LE(length, 12);
  if (endpoint.lease) header.write(endpoint.lease, 16, 'ascii');
  return header;
}
// Wait for the daemon's response before reporting readiness. A connection alone
// says nothing about microphone/codec availability or device selection.
export function openAudio(endpoint: AudioEndpoint, header: Buffer): Promise<{ socket: net.Socket; initial: Buffer }> {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: endpoint.host, port: endpoint.port, localAddress:endpoint.localAddress });
    let pending = Buffer.alloc(0);
    const timer = setTimeout(() => fail(Error('MK20 audio readiness timed out')), 4000);
    const fail = (error: Error) => { clearTimeout(timer); socket.destroy(); reject(error); };
    const receive = (data: Buffer) => {
      pending = Buffer.concat([pending, data]);
      if (pending.length < 4) return;
      if (pending.toString('ascii', 0, 4) !== 'RDY1') return fail(Error('MK20 audio rejected operation (busy, unselected or codec unavailable)'));
      clearTimeout(timer); socket.off('data', receive); socket.off('error', fail); socket.off('end', ended);
      resolve({ socket, initial: pending.subarray(4) });
    };
    const ended = () => fail(Error('MK20 audio ended before readiness'));
    socket.once('connect', () => socket.write(header)); socket.on('data', receive); socket.once('error', fail); socket.once('end', ended);
  });
}
