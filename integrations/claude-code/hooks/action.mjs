import net from 'node:net';
import { parseHookInput, formatHookContext } from '../lib/hook.mjs';

const brokerPath = process.env.FACEPALM_HOOK_SOCKET;
if (brokerPath) {
  let raw = '';
  for await (const chunk of process.stdin) {
    raw += chunk;
    if (Buffer.byteLength(raw) > 65536) process.exit(0);
  }
  const event = parseHookInput(raw);
  if (event) {
    const socket = net.createConnection(brokerPath);
    socket.setEncoding('utf8');
    socket.setTimeout(2500, () => socket.destroy());
    socket.on('error', () => socket.destroy());
    socket.on('connect', () => socket.write(`${JSON.stringify(event)}\n`));
    let buffer = '';
    socket.on('data', (chunk) => {
      buffer += chunk;
      if (Buffer.byteLength(buffer) > 65536) return socket.destroy();
      const newline = buffer.indexOf('\n');
      if (newline < 0) return;
      let reply;
      try { reply = JSON.parse(buffer.slice(0, newline)); } catch { return socket.destroy(); }
      if (!reply.ok || !reply.delivery) return socket.end();
      const result = formatHookContext(event.event, reply.delivery, event.toolUse);
      if (!result) return socket.end('ack\n');
      process.stdout.write(`${JSON.stringify(result)}\n`, (error) => {
        if (error) socket.destroy();
        else socket.end('ack\n');
      });
    });
  }
}
