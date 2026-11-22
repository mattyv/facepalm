import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, readFile, rm, stat } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';

const adapter = process.env.FACEPALM_MCP_ADAPTER ?? 'mcp/main.js';
const fixturePath = process.env.MCP_FIXTURE ?? './build/mcp_service_fixture';

test('MCP reads, reads a beat and tells its feeling to the real synthetic service per session', { timeout: 15000 }, async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fp-mcp-'));
  const socketPath = path.join(dir, 's');
  const feelingsPath = path.join(dir, 'state', 'feelings');
  const fixture = spawn(fixturePath, [socketPath, feelingsPath]);
  const fixtureExited = once(fixture, 'exit');
  const children = [];
  const lines = createInterface({ input: fixture.stdout })[Symbol.asyncIterator]();
  t.after(async () => {
    for (const { child } of children) {
      if (child.exitCode === null) child.kill();
    }
    if (fixture.exitCode === null) fixture.kill();
    await Promise.all([...children.map(({ exited }) => exited), fixtureExited]);
    await rm(dir, { recursive: true, force: true });
  });
  assert.equal((await lines.next()).value, 'ready');

  async function session() {
    const child = spawn(process.execPath, [adapter, socketPath]);
    const exited = once(child, 'exit');
    children.push({ child, exited });
    const replies = createInterface({ input: child.stdout })[Symbol.asyncIterator]();
    let id = 0;
    async function call(method, params) {
      const requestId = ++id;
      child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: requestId, method, params }) + '\n');
      const reply = JSON.parse((await replies.next()).value);
      assert.equal(reply.id, requestId);
      assert.equal(reply.error, undefined);
      return reply.result;
    }
    await call('initialize', {
      protocolVersion: '2024-11-05', capabilities: {},
      clientInfo: { name: 'integration-test', version: '1.0.0' },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
    return { child, call, exited };
  }
  const first = await session();
  const wait = (session, timeout_ms) => session.call('tools/call', {
    name: 'wait_for_reaction', arguments: { timeout_ms },
  });
  const tell = (session, id, feeling) => session.call('tools/call', {
    name: 'tell_feeling', arguments: { id, feeling },
  });
  const unpack = (result) => JSON.parse(result.content[0].text);
  assert.deepEqual(unpack(await wait(first, 0)).readings, []);
  const pending = wait(first, 5000);
  fixture.stdin.write('react\n');
  assert.equal((await lines.next()).value, 'reacted 1');
  const delivery = unpack(await pending);
  assert.equal(delivery.readings.length, 1);
  assert.equal(delivery.readings[0].id, 1);
  assert.equal(delivery.readings[0].summary, 'synthetic nod');
  assert.deepEqual(delivery.readings[0].detail, { gesture: 'nod' });
  assert.equal(delivery.readings[0].published_at_ms, 1763658000123);
  assert.deepEqual(unpack(await wait(first, 0)).readings, []);

  // A beat: the nod made inside its window is read as agreement.
  const beat = first.call('tools/call', {
    name: 'wait_for_reaction', arguments: { beat: 'question', timeout_ms: 3000 },
  });
  assert.equal((await lines.next()).value, 'beat');
  fixture.stdin.write('react\n');
  assert.equal((await lines.next()).value, 'reacted 2');
  const beaten = unpack(await beat);
  assert.deepEqual(beaten.readings.map((reading) => reading.id), [2]);
  const feelings = ['agreement', 'disagreement', 'engagement', 'annoyance', 'confused', 'unknown'];
  assert.deepEqual(beaten.interpretation, { id: 1, feeling: 'agreement', beat: 'question', gaze: 'unknown', feelings });

  // The person says how they felt: taken once, and kept in their own file.
  assert.notEqual((await tell(first, 1, 'engagement')).isError, true);
  assert.equal((await tell(first, 1, 'engagement')).isError, true);
  assert.equal((await tell(first, 999, 'engagement')).isError, true);
  assert.equal((await tell(first, 1, 'happy')).isError, true);
  assert.equal(await readFile(feelingsPath, 'utf8'), 'facepalm feelings 1\nnod question engagement 1\n');
  assert.equal((await stat(feelingsPath)).mode & 0o777, 0o600);
  const quiet = unpack(await first.call('tools/call', {
    name: 'wait_for_reaction', arguments: { beat: 'proposal', timeout_ms: 1 },
  }));
  assert.equal((await lines.next()).value, 'beat');
  assert.deepEqual(quiet.interpretation, { id: 2, feeling: 'unknown', beat: 'proposal', gaze: 'unknown', feelings });

  const second = await session();
  assert.deepEqual(unpack(await wait(second, 0)).readings, []);
  assert.equal((await tell(second, 2, 'confused')).isError, true);
  first.child.stdin.end();
  second.child.stdin.end();
  assert.deepEqual(await Promise.all([first.exited, second.exited]), [[0, null], [0, null]]);
  fixture.stdin.end();
  assert.deepEqual(await fixtureExited, [0, null]);
});
