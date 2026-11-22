import { closeSync, constants, fstatSync, openSync, readSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { createServer } from './server.js';
import { openFacepalmSettings } from './settings-launcher.js';
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

// Whether `error` is readClientToken's own ENOENT, from a token path that
// simply has nothing there yet: Facepalm has not approved this adapter,
// which is not a reason to exit (#117), unlike every other way reading a
// token file can fail (bad permissions, a symlink, the wrong length or
// format), which still is.
function tokenFileMissing(error) {
  return error?.code === 'ENOENT';
}

// Where facepalm keeps its per-user files, as src/user_paths.cc has it.
function userDirectory() {
  return process.platform === 'darwin'
    ? path.join(os.homedir(), 'Library', 'Application Support', 'Facepalm')
    : path.join(os.homedir(), '.local', 'state', 'facepalm');
}

const args = process.argv.slice(2);
if (args.length !== 0 && args.length !== 1 && !(args.length === 3 && args[1] === '--client-token-file')) {
  console.error('Usage: node mcp/main.js [SOCKET_PATH [--client-token-file PATH]]');
  process.exit(1);
}
// With no arguments, the service's socket and this adapter's approved token
// are in their fixed per-user places (#37).
const socketPath = args[0] ?? path.join(userDirectory(), 'service.sock');
const tokenPath = args.length === 0 ? path.join(userDirectory(), 'clients', 'mcp') : args[2];
if (tokenPath !== undefined) {
  try {
    readClientToken(tokenPath);
  } catch (error) {
    if (!tokenFileMissing(error)) {
      console.error(args.length === 0
        ? 'Facepalm has not approved this adapter: turn Facepalm on from its menu, or run facepalm-daemon approve mcp.'
        : 'Could not read a valid private client token file.');
      process.exit(1);
    }
    // No token file yet (#117): starts anyway. SocketSession re-reads
    // this path on every connection attempt, so approving this adapter
    // later, with no restart, is enough.
  }
}
const session = new SocketSession(socketPath, tokenPath === undefined ? undefined : () => readClientToken(tokenPath));
let broker;
if (process.env.FACEPALM_HOOK_SOCKET) {
  try { broker = await startHookBroker({ path: process.env.FACEPALM_HOOK_SOCKET, session }); }
  catch { console.error('Facepalm hooks unavailable; MCP remains available.'); }
}
const server = createServer(session, { openSettings: openFacepalmSettings });
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
