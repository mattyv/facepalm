import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { mkdtemp, rm } from 'node:fs/promises';
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
    await Promise.all([a.request('feel 1 agreement'), a.request('feel 2 agreement'), b.request('feel 3 agreement')]);
    assert.notEqual(a.name, b.name);
    assert.equal(f.connections.length, 2);
    assert.ok(commands.indexOf('feel 1 agreement') < commands.indexOf('feel 2 agreement'));
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
    const queued = session.request('feel 1 agreement', controller.signal);
    controller.abort();
    await assert.rejects(queued);
    held.write('{"readings":[],"missed":0}\n');
    assert.deepEqual(await first, { readings: [], missed: 0 });
    assert.deepEqual(await session.request('feel 2 agreement'), { ok: true });
    assert.deepEqual(commands, ['wait 5000', 'feel 2 agreement']);
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
    await assert.rejects(session.request('feel 1 agreement'));
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
    const queued = Array.from({ length: 15 }, () => session.request('feel 1 agreement'));
    await assert.rejects(session.request('feel 1 agreement'));
    held.write('{"readings":[],"missed":0}\n');
    await first;
    await Promise.all(queued);
    assert.deepEqual(await session.request('feel 2 agreement'), { ok: true });
  } finally { session.close(); await f.close(); }
});

test('accepts a fragmented reply', async () => {
  const f = await fixture((line, socket) => { socket.write('{"o'); setImmediate(() => socket.write('k":true}\n')); });
  const session = new SocketSession(f.address);
  try { assert.deepEqual(await session.request('feel 1 agreement'), { ok: true }); }
  finally { session.close(); await f.close(); }
});

test('timed-out request closes the session without reconnecting', async () => {
  const f = await fixture(() => {});
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('wait 0', undefined, 25));
    await assert.rejects(session.request('feel 1 agreement'));
    assert.equal(f.connections.length, 1);
  } finally { session.close(); await f.close(); }
});


test('a socket error, such as nothing listening, closes the session', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'facepalm-mcp-'));
  const session = new SocketSession(path.join(directory, 'nothing-listens-here.sock'));
  try {
    await assert.rejects(session.request('wait 0'));
    await assert.rejects(session.request('feel 1 agreement'));
  } finally { session.close(); await rm(directory, { recursive: true, force: true }); }
});

test('refused hello and pre-cancelled calls do not send requests', async () => {
  const commands = [];
  const f = await fixture((line) => commands.push(line), '{"error":"refused"}\n');
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('wait 0'));
    await assert.rejects(session.request('feel 1 agreement'));
    assert.deepEqual(commands, []);
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(session.request('wait 0', controller.signal));
    await assert.rejects(session.exchange('wait 0', controller.signal, 100));
  } finally { session.close(); await f.close(); }
});
