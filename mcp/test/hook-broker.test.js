import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { startHookBroker } from '../hook-broker.js';

const execFileAsync = promisify(execFile);

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
  post.socket.end('ack\n'); await once(post.socket, 'close');
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
  second.socket.end('ack\n'); await once(second.socket, 'close');
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
  next.socket.end('ack\n'); await once(next.socket, 'close');
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
  result.socket.end('ack\n'); await once(result.socket, 'close');
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

test('a malformed native delivery or reading is refused, not relayed', async (t) => {
  for (const delivery of [
    { readings: 'not-an-array', missed: 0 },
    { readings: [{ id: 0, summary: 'x', detail: {} }], missed: 0 },
  ]) {
    const socketPath = await setup(t, { queued: 0, request: async () => delivery });
    const result = await request(socketPath, event('PostToolUse'));
    assert.equal(result.result.ok, false);
    result.socket.destroy();
  }
});

test('refuses a socket path that already exists', async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const socketPath = path.join(dir, 's');
  await writeFile(socketPath, '');
  await assert.rejects(startHookBroker({ path: socketPath, session: {} }), /already exists/);
});

test('fails outright when the socket path cannot be inspected for another reason', async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const socketPath = path.join(dir, 'x'.repeat(300));
  await assert.rejects(startHookBroker({ path: socketPath, session: {} }), /name too long/i);
});

test('fails outright, and leaves no listening socket, when it cannot be secured after creation', async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const socketPath = path.join(dir, 's');
  // This file's own top-level import already linked the real hook-broker.js
  // against the real node:fs/promises, before any test body runs, so
  // mocking chmod in-process here would be too late. The fixture process
  // mocks it before importing hook-broker.js itself, in a process of its
  // own, so hook-broker.js runs unmodified and its coverage still
  // attributes to the one real file.
  // Lives outside test/ (fixtures/, a sibling of it): node --test's default
  // discovery also recurses into any directory literally named test, and
  // would otherwise run this helper itself as a bodyless test file.
  const fixture = fileURLToPath(new URL('../fixtures/chmod-denied.mjs', import.meta.url));
  await execFileAsync(process.execPath, ['--experimental-test-module-mocks', fixture, socketPath]);
});

test('destroys a connection that sends nothing within its idle timeout', { timeout: 10000 }, async (t) => {
  const socketPath = await setup(t, { queued: 0, request: () => assert.fail('must not read') });
  const socket = net.createConnection(socketPath);
  await once(socket, 'connect');
  await once(socket, 'close');
});

test('drops a connection whose first line overruns the byte limit before any newline', async (t) => {
  const socketPath = await setup(t, { queued: 0, request: () => assert.fail('must not read') });
  const socket = net.createConnection(socketPath);
  await once(socket, 'connect');
  socket.write('x'.repeat(65537));
  await once(socket, 'close');
});

test('a request split across two writes is read whole, and trailing bytes after a line destroy it', async (t) => {
  const socketPath = await setup(t, { queued: 0, request: async () => ({ readings: [], missed: 0 }) });
  const payload = JSON.stringify(event('PostToolUse'));
  const whole = net.createConnection(socketPath);
  const lines = createInterface({ input: whole })[Symbol.asyncIterator]();
  await once(whole, 'connect');
  whole.write(payload.slice(0, 5));
  whole.write(`${payload.slice(5)}\n`);
  assert.equal(JSON.parse((await lines.next()).value).ok, true);
  whole.end('ack\n');
  await once(whole, 'close');

  const trailing = net.createConnection(socketPath);
  await once(trailing, 'connect');
  trailing.write(`${JSON.stringify(event('PostToolUse', 'tool-2'))}\nextra`);
  await once(trailing, 'close');
});

test('evicts the oldest open action once 128 are pending', async (t) => {
  const session = { queued: 0, request: async () => ({ readings: [], missed: 0 }) };
  const socketPath = await setup(t, session);
  for (let i = 0; i < 129; i++) {
    const pre = await request(socketPath, event('PreToolUse', `tool-${i}`));
    assert.deepEqual(pre.result, { ok: true });
    pre.socket.end();
  }
  const post = await request(socketPath, event('PostToolUse', 'tool-0'));
  assert.equal(post.result.delivery.window.start, null);
  post.socket.end('ack\n'); await once(post.socket, 'close');
});

test('a native read failure during a hook is refused, not relayed', async (t) => {
  const socketPath = await setup(t, { queued: 0, request: async () => { throw new Error('/private/path'); } });
  const result = await request(socketPath, event('PostToolUse'));
  assert.equal(result.result.ok, false);
  assert.doesNotMatch(JSON.stringify(result.result), /private/);
  result.socket.destroy();
});

test('a non-JSON first line is refused, not left pending', async (t) => {
  const socketPath = await setup(t, { queued: 0, request: () => assert.fail('must not read') });
  const socket = net.createConnection(socketPath);
  const lines = createInterface({ input: socket })[Symbol.asyncIterator]();
  await once(socket, 'connect');
  socket.write('not-json\n');
  assert.equal(JSON.parse((await lines.next()).value).ok, false);
  socket.destroy();
});

test('a connection destroyed while its turn waits behind another is skipped, not processed', async (t) => {
  let release;
  let started;
  const entered = new Promise((resolve) => { started = resolve; });
  const delayed = new Promise((resolve) => { release = resolve; });
  const session = { queued: 0, request: async () => { started(); return delayed; } };
  const socketPath = await setup(t, session);
  const first = net.createConnection(socketPath);
  await once(first, 'connect');
  first.write(`${JSON.stringify(event('PostToolUse'))}\n`);
  await entered;
  const second = net.createConnection(socketPath);
  await once(second, 'connect');
  second.write(`${JSON.stringify(event('PostToolUse', 'tool-2'))}\n`);
  // Gives the broker's own 'data' handler time to run and chain this
  // connection's work onto the (shared, still-blocked) tail, so destroying
  // it now lands on the guard at the front of that work, not before it was
  // even scheduled.
  await new Promise((resolve) => setTimeout(resolve, 200));
  second.destroy();
  release({ readings: [], missed: 0 });
  await once(second, 'close');
  first.destroy();
});

test('shutdown skips every work item still waiting its turn, not only the one already running', async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  let release;
  let started;
  const entered = new Promise((resolve) => { started = resolve; });
  const delayed = new Promise((resolve) => { release = resolve; });
  const session = { queued: 0, request: async () => { started(); return delayed; } };
  const socketPath = path.join(dir, 's');
  const broker = await startHookBroker({ path: socketPath, session });
  const first = net.createConnection(socketPath);
  await once(first, 'connect');
  first.write(`${JSON.stringify(event('PostToolUse'))}\n`);
  await entered;
  const second = net.createConnection(socketPath);
  await once(second, 'connect');
  second.write(`${JSON.stringify(event('PostToolUse', 'tool-2'))}\n`);
  await new Promise((resolve) => setTimeout(resolve, 200));
  const closed = broker.close();
  release({ readings: [], missed: 0 });
  await closed;
});
