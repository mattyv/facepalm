import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';
import os from 'node:os';
import path from 'node:path';
import { chmod, mkdtemp, rm, symlink, writeFile } from 'node:fs/promises';

const main = fileURLToPath(new URL('../main.js', import.meta.url));

// Stands in for the native service: a Unix socket main.js connects to, so
// these tests exercise main.js's own CLI entry point (argument parsing, the
// client token file, and stdio transport wiring) as a real child process,
// not session.js, which has its own test file.
async function fixture(handler, hello = '{"ok":true}\n') {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'facepalm-mcp-'));
  const address = path.join(dir, 'service.sock');
  const connections = [];
  const server = net.createServer((socket) => {
    connections.push(socket);
    socket.setEncoding('utf8');
    let buffer = '';
    socket.on('data', (chunk) => {
      buffer += chunk;
      let index;
      while ((index = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, index);
        buffer = buffer.slice(index + 1);
        if (line.startsWith('hello ')) socket.write(typeof hello === 'function' ? hello(line) : hello);
        else handler(line, socket);
      }
    });
  });
  await new Promise((resolve) => server.listen(address, resolve));
  return { address, connections, async close() {
    for (const socket of connections) socket.destroy();
    await new Promise((resolve) => server.close(resolve));
    await rm(dir, { recursive: true, force: true });
  } };
}

for (const reason of ['EOF', 'oversized input']) {
  test(`stdio ${reason} closes the child socket`, { timeout: 7000 }, async () => {
    let entered;
    const waiting = new Promise((resolve) => { entered = resolve; });
    const token = 'a'.repeat(64);
    const f = await fixture((_, socket) => {
      entered();
      if (reason !== 'EOF') socket.write('{"readings":[],"missed":0}\n');
    }, (line) => {
      assert.equal(line, `hello ${token}`);
      return '{"ok":true}\n';
    });
    const tokenPath = path.join(path.dirname(f.address), 'approved-token');
    await writeFile(tokenPath, `${token}\n`, { mode: 0o600 });
    const child = spawn(process.execPath, [main, f.address, '--client-token-file', tokenPath]);
    const exited = once(child, 'exit');
    const replies = createInterface({ input: child.stdout })[Symbol.asyncIterator]();
    try {
      child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {
        protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'test', version: '1' },
      } }) + '\n');
      const init = JSON.parse((await replies.next()).value);
      assert.equal(init.id, 1);
      child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
      child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 2, method: 'tools/call', params: {
        name: 'wait_for_reaction', arguments: { timeout_ms: 60000 },
      } }) + '\n');
      await waiting;
      if (reason !== 'EOF') assert.equal(JSON.parse((await replies.next()).value).id, 2);
      const socketClosed = once(f.connections[0], 'close', { signal: AbortSignal.timeout(3000) });
      if (reason === 'EOF') {
        child.stdin.end();
      } else {
        child.stdin.write(Buffer.alloc(64 * 1024 + 1, 65));
      }
      await socketClosed;
      child.stdin.end();
      assert.deepEqual(await exited, [0, null]);
    } finally {
      if (child.exitCode === null) child.kill();
      await exited;
      await f.close();
    }
  });
}

test('requires a socket path', { timeout: 5000 }, async () => {
  const child = spawn(process.execPath, [main]);
  assert.deepEqual(await once(child, 'exit'), [1, null]);
});

test('rejects invalid or nonprivate token files', { timeout: 5000 }, async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'facepalm-token-'));
  const tokenPath = path.join(directory, 'token');
  const linkPath = path.join(directory, 'link');
  async function fails(file) {
    const child = spawn(process.execPath, [main, path.join(directory, 'socket'), '--client-token-file', file]);
    const exited = once(child, 'exit');
    const stderr = [];
    child.stderr.on('data', (data) => stderr.push(data));
    child.stdin.end();
    assert.deepEqual(await exited, [1, null]);
    assert.match(Buffer.concat(stderr).toString(), /private client token file/i);
    assert.doesNotMatch(Buffer.concat(stderr).toString(), /a{64}/);
  }
  try {
    await writeFile(tokenPath, 'a'.repeat(64), { mode: 0o600 });
    await chmod(tokenPath, 0o644);
    await fails(tokenPath);
    await chmod(tokenPath, 0o600);
    await symlink(tokenPath, linkPath);
    await fails(linkPath);
    await writeFile(tokenPath, 'a'.repeat(64) + '\n\n');
    await fails(tokenPath);
    await writeFile(tokenPath, 'invalid');
    await fails(tokenPath);
    await writeFile(tokenPath, `${'a'.repeat(64)}x`);
    await fails(tokenPath);
    await writeFile(tokenPath, 'g'.repeat(64));
    await fails(tokenPath);
    await fails(directory);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('rejects a malformed command line before touching any socket', { timeout: 5000 }, async () => {
  for (const args of [['s', '--client-token-file'], ['s', '--wrong-flag', 't'], ['s', 'extra', 'args']]) {
    const child = spawn(process.execPath, [main, ...args]);
    assert.deepEqual(await once(child, 'exit'), [1, null]);
  }
});

test('starts the hook broker only when FACEPALM_HOOK_SOCKET is set, and keeps the MCP server up when it fails', async () => {
  const f = await fixture(() => {});
  try {
    const unavailable = spawn(process.execPath, [main, f.address], {
      env: { ...process.env, FACEPALM_HOOK_SOCKET: '/nonexistent/dir/does-not-exist.sock' },
    });
    const stderr = [];
    unavailable.stderr.on('data', (data) => stderr.push(data));
    const exited = once(unavailable, 'exit');
    try {
      const init = createInterface({ input: unavailable.stdout })[Symbol.asyncIterator]();
      unavailable.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {
        protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'test', version: '1' },
      } }) + '\n');
      assert.equal(JSON.parse((await init.next()).value).id, 1);
      assert.match(Buffer.concat(stderr).toString(), /hooks unavailable/i);
    } finally {
      // Ends gracefully, through main.js's own close(), rather than killing
      // it with a signal: a signal skips the exit hook that flushes coverage.
      unavailable.stdin.end();
      await exited;
    }
  } finally { await f.close(); }
});

test('starts the hook broker and closes it with the session on shutdown', async () => {
  const f = await fixture(() => {});
  const hookDir = await mkdtemp(path.join(os.tmpdir(), 'facepalm-hook-'));
  try {
    const child = spawn(process.execPath, [main, f.address], {
      env: { ...process.env, FACEPALM_HOOK_SOCKET: path.join(hookDir, 'hook.sock') },
    });
    const stderr = [];
    child.stderr.on('data', (data) => stderr.push(data));
    const exited = once(child, 'exit');
    try {
      const init = createInterface({ input: child.stdout })[Symbol.asyncIterator]();
      child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {
        protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'test', version: '1' },
      } }) + '\n');
      assert.equal(JSON.parse((await init.next()).value).id, 1);
      assert.doesNotMatch(Buffer.concat(stderr).toString(), /hooks unavailable/i);
      child.stdin.end();
      assert.deepEqual(await exited, [0, null]);
    } finally { if (child.exitCode === null) { child.kill(); await exited; } }
  } finally { await f.close(); await rm(hookDir, { recursive: true, force: true }); }
});
