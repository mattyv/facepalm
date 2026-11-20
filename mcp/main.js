import { closeSync, constants, fstatSync, openSync, readSync } from 'node:fs';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { createServer } from './server.js';
import { SocketSession } from './session.js';
import { startHookBroker } from './hook-broker.js';

function readClientToken(path) {
  const fd = openSync(path, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK);
  try {
    const info = fstatSync(fd);
    if (!info.isFile() || info.uid !== process.getuid() || (info.mode & 0o077) !== 0) {
      throw new Error('token permissions');
    }
    const bytes = Buffer.alloc(66);
    const count = readSync(fd, bytes, 0, bytes.length, null);
    if (count !== 64 && !(count === 65 && bytes[64] === 10)) throw new Error('token length');
    const token = bytes.toString('utf8', 0, 64);
    if (!/^[a-f0-9]{64}$/.test(token)) throw new Error('token format');
    return token;
  } finally {
    closeSync(fd);
  }
}

const args = process.argv.slice(2);
if (args.length !== 1 && !(args.length === 3 && args[1] === '--client-token-file')) {
  console.error('Usage: node mcp/main.js SOCKET_PATH [--client-token-file PATH]');
  process.exit(1);
}
let token;
if (args.length === 3) {
  try {
    token = readClientToken(args[2]);
  } catch {
    console.error('Could not read a valid private client token file.');
    process.exit(1);
  }
}
const session = new SocketSession(args[0], token);
let broker;
if (process.env.FACEPALM_HOOK_SOCKET) {
  try { broker = await startHookBroker({ path: process.env.FACEPALM_HOOK_SOCKET, session }); }
  catch { console.error('Facepalm hooks unavailable; MCP remains available.'); }
}
const server = createServer(session);
const transport = new StdioServerTransport(process.stdin, process.stdout, { maxBufferSize: 64 * 1024 });
let closing = false;
function close() {
  if (closing) return;
  closing = true;
  session.close();
  void broker?.close();
}
transport.onclose = close;
process.stdin.on('end', close);
await server.connect(transport);
