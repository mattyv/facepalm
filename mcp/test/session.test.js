import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { mkdtemp, rm } from 'node:fs/promises';
import { SocketSession, OfflineError, UnapprovedError } from '../session.js';

async function fixture(handler, hello = '{"ok":true}\n', address = undefined) {
  const dir = address ? undefined : await mkdtemp(path.join(os.tmpdir(), 'facepalm-mcp-'));
  address ??= path.join(dir, 'service.sock');
  const connections = [];
  const server = net.createServer((socket) => {
    connections.push(socket);
    // A client killed mid-test resets its connection; that is not a failure.
    socket.on('error', () => {});
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
    if (dir) await rm(dir, { recursive: true, force: true });
  } };
}

// A clock a test can move forward by hand, so backoff never needs a real
// wait; starts at an arbitrary, nonzero instant, as Date.now() would.
function fakeClock(start = 1_700_000_000_000) {
  let now = start;
  const clock = () => now;
  clock.advance = (ms) => { now += ms; };
  return clock;
}

// One real timer tick: disconnect()'s own socket.destroy() tears down its
// file descriptor on a later tick of the real event loop, not the fake
// clock's; without this, a reconnect attempted on the very same tick can
// find the old socket's teardown still pending and never reach the
// fixture at all.
function settle() {
  return new Promise((resolve) => { setTimeout(resolve, 0); });
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

test('sends a token with hello when one is given, and omits it otherwise', async () => {
  const hellos = [];
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'), (line) => { hellos.push(line); return '{"ok":true}\n'; });
  const session = new SocketSession(f.address, 'a-private-token');
  try {
    await session.request('feel 1 agreement');
    assert.deepEqual(hellos, [`hello ${session.name} a-private-token`]);
  } finally { session.close(); await f.close(); }
});

test('a token given as a function is read fresh for every connection attempt (#117)', async () => {
  const hellos = [];
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'), (line) => {
    hellos.push(line);
    return line.split(' ').length === 3 ? '{"ok":true}\n' : '{"error":"client not approved"}\n';
  });
  let token;
  const clock = fakeClock();
  const session = new SocketSession(f.address, () => token, clock);
  try {
    await assert.rejects(session.request('feel 1 agreement'));
    clock.advance(session.backoffMs);
    await settle();
    token = 'approved-later';
    await session.request('feel 2 agreement');
    assert.deepEqual(hellos, [`hello ${session.name}`, `hello ${session.name} approved-later`]);
  } finally { session.close(); await f.close(); }
});

test('a token function that throws is treated as no token, not a crash', async () => {
  const hellos = [];
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'), (line) => { hellos.push(line); return '{"ok":true}\n'; });
  const session = new SocketSession(f.address, () => { throw new Error('not there yet'); });
  try {
    await session.request('feel 1 agreement');
    assert.deepEqual(hellos, [`hello ${session.name}`]);
  } finally { session.close(); await f.close(); }
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
    assert.equal(session.closed, true);
  } finally { session.close(); await f.close(); }
});

test('invalid, extra and oversized replies disconnect, but do not close, the session (#117)', async () => {
  for (const reply of ['not-json\n', '{}\n{}\n', 'x'.repeat(1024 * 1024 + 1)]) {
    const f = await fixture((line, socket) => socket.write(reply));
    const session = new SocketSession(f.address);
    try {
      await assert.rejects(session.request('wait 0'), OfflineError);
      assert.equal(session.closed, false);
      // Still backing off: the retry does not even try to dial again.
      await assert.rejects(session.request('wait 0'), OfflineError);
      assert.equal(f.connections.length, 1);
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

test('a timed-out request disconnects, but does not close, the session (#117)', async () => {
  const f = await fixture(() => {});
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('wait 0', undefined, 25), OfflineError);
    assert.equal(session.closed, false);
    await assert.rejects(session.request('feel 1 agreement'), OfflineError);
    assert.equal(f.connections.length, 1);
  } finally { session.close(); await f.close(); }
});

test('a socket error, such as nothing listening, disconnects without closing the session (#117)', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'facepalm-mcp-'));
  const session = new SocketSession(path.join(directory, 'nothing-listens-here.sock'));
  try {
    await assert.rejects(session.request('wait 0'), OfflineError);
    assert.equal(session.closed, false);
    await assert.rejects(session.request('feel 1 agreement'), OfflineError);
  } finally { session.close(); await rm(directory, { recursive: true, force: true }); }
});

test('refused hello and pre-cancelled calls do not send requests', async () => {
  const commands = [];
  const f = await fixture((line) => commands.push(line), '{"error":"refused"}\n');
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('wait 0'), OfflineError);
    assert.equal(session.closed, false);
    await assert.rejects(session.request('feel 1 agreement'), OfflineError);
    assert.deepEqual(commands, []);
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(session.request('wait 0', controller.signal));
    await assert.rejects(session.exchange('wait 0', controller.signal, 100));
  } finally { session.close(); await f.close(); }
});

test('reconnects once its backoff has run out, without the caller doing anything (#117)', async () => {
  const commands = [];
  let healthy = false;
  const f = await fixture((line, socket) => {
    if (!healthy) return socket.end();
    commands.push(line);
    socket.write('{"ok":true}\n');
  });
  const clock = fakeClock();
  const session = new SocketSession(f.address, undefined, clock);
  try {
    await assert.rejects(session.request('feel 1 agreement'), OfflineError);
    assert.equal(session.closed, false);
    // Still inside the backoff window: no new connection is attempted.
    await assert.rejects(session.request('feel 2 agreement'), OfflineError);
    assert.equal(f.connections.length, 1);
    healthy = true;
    clock.advance(1000);
    await settle();
    assert.deepEqual(await session.request('feel 3 agreement'), { ok: true });
    assert.deepEqual(commands, ['feel 3 agreement']);
    assert.equal(f.connections.length, 2);
  } finally { session.close(); await f.close(); }
});

test('backoff grows on each failure in a row, and resets once a connection succeeds (#117)', async () => {
  let healthy = false;
  const f = await fixture(
    (line, socket) => socket.write('{"ok":true}\n'),
    () => (healthy ? '{"ok":true}\n' : '{"error":"not approved yet"}\n'),
  );
  const clock = fakeClock();
  const session = new SocketSession(f.address, undefined, clock);
  try {
    await assert.rejects(session.request('feel 1 agreement'));
    clock.advance(999);
    await assert.rejects(session.request('feel 2 agreement')); // Still too soon: no new attempt.
    assert.equal(f.connections.length, 1);
    clock.advance(1);
    await settle();
    await assert.rejects(session.request('feel 3 agreement')); // Backoff doubles to 2000ms.
    assert.equal(f.connections.length, 2);
    clock.advance(1999);
    await assert.rejects(session.request('feel 4 agreement')); // Still too soon for the doubled wait.
    assert.equal(f.connections.length, 2);
    clock.advance(1);
    await settle();
    healthy = true;
    await session.request('feel 5 agreement');
    assert.equal(f.connections.length, 3);
    // Backoff reset: a later failure waits the minimum again, not the
    // doubled 4000ms it would be without the reset. The link itself has
    // to drop first: an already-open connection keeps exchanging over it
    // regardless of `healthy`, which only the next hello sees.
    healthy = false;
    f.connections.at(-1).end();
    await settle();
    // Blocked immediately: the link just dropped, backing off from its
    // own reset minimum, so no new connection is even attempted yet.
    await assert.rejects(session.request('feel 6 agreement'));
    assert.equal(f.connections.length, 3);
    clock.advance(999);
    await assert.rejects(session.request('feel 7 agreement')); // Still too soon for the reset (minimum) wait.
    assert.equal(f.connections.length, 3);
    clock.advance(1);
    await settle();
    await assert.rejects(session.request('feel 8 agreement')); // The reset wait has now passed.
    assert.equal(f.connections.length, 4);
  } finally { session.close(); await f.close(); }
});

test('an unapproved hello rejects with UnapprovedError, not OfflineError (#129)', async () => {
  const commands = [];
  const f = await fixture((line) => commands.push(line), '{"error":"client not approved"}\n');
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('feel 1 agreement'), UnapprovedError);
    assert.equal(session.closed, false);
    assert.deepEqual(commands, []);
  } finally { session.close(); await f.close(); }
});

test('a different hello refusal still rejects with OfflineError, not UnapprovedError (#129)', async () => {
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'), '{"error":"refused"}\n');
  const session = new SocketSession(f.address);
  try {
    await assert.rejects(session.request('feel 1 agreement'), OfflineError);
  } finally { session.close(); await f.close(); }
});

test('a stale socket close cannot tear down the socket that replaced it (#129)', async () => {
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'));
  const session = new SocketSession(f.address);
  let stale;
  try {
    await session.request('feel 1 agreement');
    stale = session.socket;
    const sentinel = { destroy() {} };
    session.socket = sentinel;
    stale.emit('close');
    assert.equal(session.socket, sentinel);
  } finally { stale?.destroy(); session.close(); await f.close(); }
});

test('a stale socket error cannot tear down the socket that replaced it (#129)', async () => {
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'));
  const session = new SocketSession(f.address);
  let stale;
  try {
    await session.request('feel 1 agreement');
    stale = session.socket;
    const sentinel = { destroy() {} };
    session.socket = sentinel;
    stale.emit('error', new Error('stale'));
    assert.equal(session.socket, sentinel);
  } finally { stale?.destroy(); session.close(); await f.close(); }
});

test('an interpretation id is forgotten once the link is lost, and known again once relearned (#129)', async () => {
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'));
  const session = new SocketSession(f.address);
  try {
    assert.equal(session.knowsInterpretation(1), false);
    session.noteInterpretation(1, 'agreement');
    assert.equal(session.knowsInterpretation(1), true);
    session.disconnect();
    assert.equal(session.knowsInterpretation(1), false);
    assert.equal(session.interpretation(1), undefined);
    session.noteInterpretation(1, 'agreement');
    await session.request('feel 1 agreement');
    assert.equal(session.knowsInterpretation(1), true);
  } finally { session.close(); await f.close(); }
});

test('interpretation() returns the read a noted id was given, with no pick yet (#133)', () => {
  const session = new SocketSession('/does-not-matter');
  try {
    assert.equal(session.interpretation(1), undefined);
    session.noteInterpretation(1, 'agreement');
    assert.deepEqual(session.interpretation(1), { read: 'agreement', picked: undefined });
  } finally { session.close(); }
});

test('recordPick() records a pick for an id this session knows, leaving its read untouched (#133)', () => {
  const session = new SocketSession('/does-not-matter');
  try {
    session.noteInterpretation(1, 'agreement');
    session.recordPick(1, 'disagreement');
    assert.deepEqual(session.interpretation(1), { read: 'agreement', picked: 'disagreement' });
  } finally { session.close(); }
});

test('recordPick() does nothing for an id this session does not know (#133)', () => {
  const session = new SocketSession('/does-not-matter');
  try {
    session.recordPick(1, 'disagreement');
    assert.equal(session.interpretation(1), undefined);
  } finally { session.close(); }
});

test('a pick is forgotten along with its read once the link is lost (#133)', async () => {
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'));
  const session = new SocketSession(f.address);
  try {
    session.noteInterpretation(1, 'agreement');
    session.recordPick(1, 'disagreement');
    session.disconnect();
    assert.equal(session.interpretation(1), undefined);
  } finally { session.close(); await f.close(); }
});

test('offline never bounds beyond its own cap', async () => {
  const f = await fixture((line, socket) => socket.write('{"ok":true}\n'), () => '{"error":"refused"}\n');
  const clock = fakeClock();
  const session = new SocketSession(f.address, undefined, clock);
  try {
    let attempts = 1;
    await assert.rejects(session.request('feel 1 agreement'));
    for (let doublings = 0; doublings < 10; doublings++) {
      clock.advance(session.backoffMs);
      await settle();
      await assert.rejects(session.request(`feel ${++attempts} agreement`));
    }
    assert.equal(session.backoffMs, 30000);
  } finally { session.close(); await f.close(); }
});
