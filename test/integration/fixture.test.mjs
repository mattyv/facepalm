import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';

// Exercise the real feed and service before testing the MCP adapter.
test('synthetic fixture serves the real socket protocol', { timeout: 10000 }, async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fp-mcp-'));
  const socketPath = path.join(dir, 's');
  const fixture = spawn(process.env.MCP_FIXTURE ?? './build/mcp_service_fixture', [socketPath, path.join(dir, 'feelings')]);
  const exited = once(fixture, 'exit');
  const output = createInterface({ input: fixture.stdout })[Symbol.asyncIterator]();
  t.after(async () => {
    if (fixture.exitCode === null) fixture.kill();
    await exited;
    await rm(dir, { recursive: true, force: true });
  });
  assert.equal((await output.next()).value, 'ready');
  const socket = net.createConnection(socketPath);
  t.after(() => socket.destroy());
  await once(socket, 'connect');
  const replies = createInterface({ input: socket })[Symbol.asyncIterator]();
  socket.write('hello agent\n');
  assert.deepEqual(JSON.parse((await replies.next()).value), { ok: true });
  fixture.stdin.write('react\n');
  assert.equal((await output.next()).value, 'reacted 1');
  socket.write('read\n');
  const delivery = JSON.parse((await replies.next()).value);
  assert.equal(delivery.readings[0].id, 1);
  assert.equal(delivery.readings[0].summary, 'synthetic nod');
  socket.write('reset feelings\n');
  assert.deepEqual(JSON.parse((await replies.next()).value), { ok: true });
  socket.write('rate 1 5\n');
  assert.deepEqual(JSON.parse((await replies.next()).value), { error: 'unknown request' });
  fixture.stdin.end();
  assert.deepEqual(await exited, [0, null]);
});
