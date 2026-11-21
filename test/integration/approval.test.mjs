import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';

const adapter = process.env.FACEPALM_MCP_ADAPTER ?? 'mcp/main.js';
const fixturePath = process.env.MCP_FIXTURE ?? './build/mcp_service_fixture';

// Two MCP adapters showing the one approved token are two clients, both
// told of the same nod, and one stranger is refused (#37). The fixture
// admits clients through the daemon's ClientApprovals and ApprovedFeed.
test('two approved MCP sessions get the same nod, each its own beat, and a stranger none', { timeout: 15000 }, async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fp-approval-'));
  const clients = path.join(dir, 'clients');
  const socketPath = path.join(dir, 's');
  await mkdir(clients, { mode: 0o700 });
  const tokenPath = path.join(clients, 'mcp');
  await writeFile(tokenPath, `${'c'.repeat(64)}\n`, { mode: 0o600 });
  const fixture = spawn(fixturePath, ['--clients', clients, socketPath]);
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

  const stranger = net.createConnection(socketPath);
  t.after(() => stranger.destroy());
  const strangerReplies = createInterface({ input: stranger })[Symbol.asyncIterator]();
  stranger.write(`hello stranger ${'d'.repeat(64)}\n`);
  assert.deepEqual(JSON.parse((await strangerReplies.next()).value), { error: 'client not approved' });

  async function session() {
    const child = spawn(process.execPath, [adapter, socketPath, '--client-token-file', tokenPath]);
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
      clientInfo: { name: 'approval-test', version: '1.0.0' },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
    return { child, call, exited };
  }
  const wait = (session, timeout_ms) => session.call('tools/call', {
    name: 'wait_for_reaction', arguments: { timeout_ms },
  });
  const unpack = (result) => JSON.parse(result.content[0].text);

  const desktop = await session();
  const code = await session();
  assert.deepEqual(unpack(await wait(desktop, 0)).readings, []);
  assert.equal((await lines.next()).value, 'camera on');
  assert.deepEqual(unpack(await wait(code, 0)).readings, []);

  const desktopWait = wait(desktop, 5000);
  const codeWait = wait(code, 5000);
  fixture.stdin.write('react\n');
  assert.equal((await lines.next()).value, 'reacted 1');
  for (const delivery of [unpack(await desktopWait), unpack(await codeWait)]) {
    assert.equal(delivery.readings.length, 1);
    assert.equal(delivery.readings[0].id, 1);
    assert.deepEqual(delivery.readings[0].detail, { gesture: 'nod' });
  }

  // A beat is one client's own: the client in a beat is told how the nod
  // was read, and the other gets the same nod with no interpretation.
  const desktopBeat = desktop.call('tools/call', {
    name: 'wait_for_reaction', arguments: { beat: 'question', timeout_ms: 3000 },
  });
  assert.equal((await lines.next()).value, 'beat');
  const codeAgain = wait(code, 5000);
  fixture.stdin.write('react\n');
  assert.equal((await lines.next()).value, 'reacted 2');
  const beaten = unpack(await desktopBeat);
  assert.deepEqual(beaten.readings.map((reading) => reading.id), [2]);
  assert.deepEqual(beaten.interpretation, { id: 1, feeling: 'agreement', beat: 'question' });
  const plain = unpack(await codeAgain);
  assert.deepEqual(plain.readings.map((reading) => reading.id), [2]);
  assert.equal(plain.interpretation, undefined);

  desktop.child.stdin.end();
  await desktop.exited;
  // An adapter that dies mid-wait gives up the camera at once, not when its
  // minute-long wait would have timed out.
  const longWait = wait(code, 60000).catch(() => {});
  await new Promise((resolve) => setTimeout(resolve, 200));
  const killed = Date.now();
  code.child.kill('SIGKILL');
  await code.exited;
  assert.equal((await lines.next()).value, 'camera off');
  assert.ok(Date.now() - killed < 5000);
  await longWait;
  fixture.stdin.end();
  assert.deepEqual(await fixtureExited, [0, null]);
});
