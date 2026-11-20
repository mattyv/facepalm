import assert from 'node:assert/strict';
import { once } from 'node:events';
import { mkdtemp, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';
import { startHookBroker } from '../hook-broker.js';

const event = (eventName, id = 'tool-1') => ({
  event: eventName, session: 'session-1', toolUse: id, tool: 'Bash',
});

async function setup(t, session) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  const socket = path.join(dir, 's');
  const broker = await startHookBroker({ path: socket, session });
  t.after(async () => { await broker.close(); await rm(dir, { recursive: true, force: true }); });
  return socket;
}
async function request(socketPath, value) {
  const socket = net.createConnection(socketPath);
  const lines = createInterface({ input: socket })[Symbol.asyncIterator]();
  await once(socket, 'connect');
  socket.write(`${JSON.stringify(value)}\n`);
  const result = JSON.parse((await lines.next()).value);
  return { socket, result };
}

test('hooks use the owner session and acknowledge delivered readings', async (t) => {
  const calls = [];
  const session = { queued: 0, request: async (...args) => {
    calls.push(args[0]); return { readings: [{ id: 1, summary: 'nod', detail: {}, published_at_ms: Date.now() }], missed: 0 };
  } };
  const path = await setup(t, session);
  const pre = await request(path, event('PreToolUse'));
  assert.deepEqual(pre.result, { ok: true });
  pre.socket.end();
  const post = await request(path, event('PostToolUse'));
  assert.equal(post.result.delivery.readings[0].id, 1);
  assert.deepEqual(calls, ['wait 0']);
  post.socket.end('ack\n');
});

test('does not queue a hook read behind an active MCP request', async (t) => {
  const session = { queued: 1, request: () => assert.fail('must not read') };
  const path = await setup(t, session);
  const response = await request(path, event('PostToolUse'));
  assert.equal(response.result.busy, true);
  response.socket.destroy();
});

test('replays an unacknowledged result without reading twice', async (t) => {
  let calls = 0;
  const session = { queued: 0, request: async () => {
    calls++; return { readings: [{ id: 3, summary: 'nod', detail: {} }], missed: 0 };
  } };
  const path = await setup(t, session);
  const first = await request(path, event('PostToolUse'));
  first.socket.destroy();
  const second = await request(path, event('PostToolUse', 'tool-2'));
  assert.equal(second.result.delivery.readings[0].id, 3);
  assert.equal(calls, 1);
  second.socket.end('ack\n');
});

test('refuses another host session and invalid input', async (t) => {
  const session = { queued: 0, request: () => assert.fail('must not read') };
  const path = await setup(t, session);
  const first = await request(path, event('PreToolUse'));
  first.socket.end();
  const other = await request(path, { ...event('PreToolUse'), session: 'different' });
  assert.equal(other.result.ok, false);
  other.socket.destroy();
});

test('preserves a read when the hook disconnects before native reply', async (t) => {
  let release;
  let started;
  const entered = new Promise((resolve) => { started = resolve; });
  const delayed = new Promise((resolve) => { release = resolve; });
  let calls = 0;
  const session = { queued: 0, request: async () => { calls++; started(); return delayed; } };
  const socketPath = await setup(t, session);
  const first = net.createConnection(socketPath);
  await once(first, 'connect');
  first.write(`${JSON.stringify(event('PostToolUse'))}\n`);
  await entered;
  first.destroy();
  release({ readings: [{ id: 5, summary: 'smile', detail: {} }], missed: 0 });
  await once(first, 'close');
  const next = await request(socketPath, event('PostToolUse', 'tool-2'));
  assert.equal(next.result.delivery.readings[0].id, 5);
  assert.equal(calls, 1);
  next.socket.end('ack\n');
});

test('refuses a public directory without creating a socket', async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const { chmod } = await import('node:fs/promises');
  await chmod(dir, 0o755);
  await assert.rejects(startHookBroker({ path: path.join(dir, 's'), session: {} }), /private/);
});

test('compacts oversized delivery and reports omitted readings', async (t) => {
  const readings = Array.from({ length: 1000 }, (_, index) => ({
    id: index + 1, summary: 'x'.repeat(4096), detail: {},
  }));
  const session = { queued: 0, request: async () => ({ readings, missed: 0 }) };
  const socketPath = await setup(t, session);
  const result = await request(socketPath, event('PostToolUse'));
  assert.ok(Buffer.byteLength(JSON.stringify(result.result)) < 65536);
  assert.equal(result.result.delivery.readings.length, 10);
  assert.equal(result.result.delivery.omitted, 990);
  assert.ok(result.result.delivery.readings[0].summary.endsWith('…'));
  result.socket.end('ack\n');
});

test('shutdown waits for a dispatched native read to retire', async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  let release;
  let started;
  const entered = new Promise((resolve) => { started = resolve; });
  const delayed = new Promise((resolve) => { release = resolve; });
  const session = { queued: 0, request: async () => { started(); return delayed; } };
  const socketPath = path.join(dir, 's');
  const broker = await startHookBroker({ path: socketPath, session });
  const peer = net.createConnection(socketPath);
  await once(peer, 'connect');
  peer.write(`${JSON.stringify(event('PostToolUse'))}\n`);
  await entered;
  let retired = false;
  const closed = broker.close().then(() => { retired = true; });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(retired, false);
  release({ readings: [], missed: 0 });
  await closed;
  assert.equal(retired, true);
});
