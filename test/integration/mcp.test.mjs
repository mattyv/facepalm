import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, readFile, rm, truncate } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';

const adapter = process.env.FACEPALM_MCP_ADAPTER ?? 'mcp/main.js';
const fixturePath = process.env.MCP_FIXTURE ?? './build/mcp_service_fixture';

test('MCP reads and rates the real synthetic service per session', { timeout: 15000 }, async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fp-mcp-'));
  const socketPath = path.join(dir, 's');
  const feedbackPath = path.join(dir, 'ratings.jsonl');
  const fixture = spawn(fixturePath, [socketPath, feedbackPath]);
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
  const rate = (session, id, score) => session.call('tools/call', {
    name: 'rate_reaction', arguments: { id, score },
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
  assert.notEqual((await rate(first, 1, 5)).isError, true);
  assert.match((await lines.next()).value, /^rated \S+ 1 5$/);
  const records = (await readFile(feedbackPath, 'utf8')).trim().split('\n').map(JSON.parse);
  assert.equal(records.length, 1);
  assert.equal(records[0].id, 1);
  assert.equal(records[0].score, 5);
  assert.equal(records[0].summary, 'synthetic nod');
  assert.equal(records[0].published_at_ms, 1763658000123);
  assert.equal(typeof records[0].rated_at_ms, 'number');
  assert.equal(records[0].version, 1);
  assert.deepEqual(Object.keys(records[0]).sort(),
    ['id', 'published_at_ms', 'rated_at_ms', 'score', 'summary', 'version']);
  await truncate(feedbackPath, 10 * 1024 * 1024);
  assert.equal((await rate(first, 1, 4)).isError, true);
  assert.equal((await rate(first, 999, 5)).isError, true);

  const second = await session();
  assert.deepEqual(unpack(await wait(second, 0)).readings, []);
  assert.equal((await rate(second, 1, 4)).isError, true);
  first.child.stdin.end();
  second.child.stdin.end();
  assert.deepEqual(await Promise.all([first.exited, second.exited]), [[0, null], [0, null]]);
  fixture.stdin.end();
  assert.deepEqual(await fixtureExited, [0, null]);
});
