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
import { SocketSession } from '../session.js';

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

test('serializes requests on one uniquely named connection', async () => {
  const commands = [];
  const f = await fixture((line, socket) => { commands.push(line); socket.write('{"ok":true}\n'); });
  const a = new SocketSession(f.address);
  const b = new SocketSession(f.address);
  try {
    await Promise.all([a.request('rate 1 3'), a.request('rate 2 4'), b.request('rate 3 5')]);
    assert.notEqual(a.name, b.name);
    assert.equal(f.connections.length, 2);
    assert.ok(commands.indexOf('rate 1 3') < commands.indexOf('rate 2 4'));
  } finally { a.close(); b.close(); await f.close(); }
});

test('cancelling a queued request does not close the active one', async () => {
  let entered;
  const waiting = new Promise((resolve) => { entered = resolve; });
  let held;
  const commands = [];
  const f = await fixture((line, socket) => { commands.push(line); if (line.startsWith('wait')) { held = socket; entered(); } else socket.write('{"ok":true}\n'); });
  const session = new SocketSession(f.address);
  try {
    const first = session.request('wait 5000');
    await waiting;
    const controller = new AbortController();
    const queued = session.request('rate 1 4', controller.signal);
    controller.abort();
    await assert.rejects(queued);
    held.write('{"readings":[],"missed":0}\n');
    assert.deepEqual(await first, { readings: [], missed: 0 });
    assert.deepEqual(await session.request('rate 2 5'), { ok: true });
    assert.deepEqual(commands, ['wait 5000', 'rate 2 5']);
  } finally { session.close(); await f.close(); }
});

test('active cancellation is terminal and never reconnects', async () => {
  let entered;
  const waiting = new Promise((resolve) => { entered = resolve; });
  const f = await fixture(() => entered());
  const session = new SocketSession(f.address);
  try {
    const controller = new AbortController();
    const call = session.request('wait 5000', controller.signal);
    await waiting;
    controller.abort();
    await assert.rejects(call);
    await assert.rejects(session.request('rate 1 5'));
    assert.equal(f.connections.length, 1);
  } finally { session.close(); await f.close(); }
});

test('invalid, extra and oversized replies end the session', async () => {
  for (const reply of ['not-json\n', '{}\n{}\n', 'x'.repeat(1024 * 1024 + 1)]) {
    const f = await fixture((line, socket) => socket.write(reply));
    const session = new SocketSession(f.address);
    try {
      await assert.rejects(session.request('wait 0'));
      await assert.rejects(session.request('wait 0'));
    } finally { session.close(); await f.close(); }
  }
});

test('bounds pending requests and recovers after they settle', async () => {
  let entered;
  const waiting = new Promise((resolve) => { entered = resolve; });
  let held;
  const f = await fixture((line, socket) => { if (line.startsWith('wait')) { held = socket; entered(); } else socket.write('{"ok":true}\n'); });
  const session = new SocketSession(f.address);
  try {
    const first = session.request('wait 5000');
    await waiting;
    const queued = Array.from({ length: 15 }, () => session.request('rate 1 3'));
    await assert.rejects(session.request('rate 1 3'));
    held.write('{"readings":[],"missed":0}\n');
    await first;
    await Promise.all(queued);
    assert.deepEqual(await session.request('rate 2 4'), { ok: true });
  } finally { session.close(); await f.close(); }
});

test('accepts a fragmented reply', async () => {
  const f = await fixture((line, socket) => { socket.write('{"o'); setImmediate(() => socket.write('k":true}\n')); });
  const session = new SocketSession(f.address);
  try { assert.deepEqual(await session.request('rate 1 5'), { ok: true }); }
  finally { session.close(); await f.close(); }
});

test('timed-out request closes the session without reconnecting', async () => {
  const f = await fixture(() => {});
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('wait 0', undefined, 25));
    await assert.rejects(session.request('rate 1 5'));
    assert.equal(f.connections.length, 1);
  } finally { session.close(); await f.close(); }
});


test('refused hello and pre-cancelled calls do not send requests', async () => {
  const commands = [];
  const f = await fixture((line) => commands.push(line), '{"error":"refused"}\n');
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('wait 0'));
    await assert.rejects(session.request('rate 1 5'));
    assert.deepEqual(commands, []);
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(session.request('wait 0', controller.signal));
    await assert.rejects(session.exchange('wait 0', controller.signal, 100));
  } finally { session.close(); await f.close(); }
});


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
    const main = fileURLToPath(new URL('../main.js', import.meta.url));
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

test('stdio entry point requires a socket path', { timeout: 5000 }, async () => {
  const main = fileURLToPath(new URL('../main.js', import.meta.url));
  const child = spawn(process.execPath, [main]);
  assert.deepEqual(await once(child, 'exit'), [1, null]);
});

test('stdio entry point rejects invalid or nonprivate token files', { timeout: 5000 }, async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'facepalm-token-'));
  const tokenPath = path.join(directory, 'token');
  const linkPath = path.join(directory, 'link');
  const main = fileURLToPath(new URL('../main.js', import.meta.url));
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
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
