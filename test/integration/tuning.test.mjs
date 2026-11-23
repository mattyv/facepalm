import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, readFile, rm, stat } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';

const fixturePath = process.env.MCP_FIXTURE ?? './build/mcp_service_fixture';
const frown = '{"expression":"brows_down"}';

// Starts the synthetic service over `feelingsPath`, connected as one client.
async function start(t, socketPath, feelingsPath) {
  const fixture = spawn(fixturePath, [socketPath, feelingsPath]);
  const exited = once(fixture, 'exit');
  const output = createInterface({ input: fixture.stdout })[Symbol.asyncIterator]();
  assert.equal((await output.next()).value, 'ready');
  const socket = net.createConnection(socketPath);
  await once(socket, 'connect');
  const replies = createInterface({ input: socket })[Symbol.asyncIterator]();
  const ask = async (line) => {
    socket.write(`${line}\n`);
    return JSON.parse((await replies.next()).value);
  };
  assert.deepEqual(await ask('hello agent'), { ok: true });
  // A beat in which the person makes `reaction`; its interpretation.
  const beat = async (kind, reaction) => {
    socket.write(`beat ${kind} 300\n`);
    assert.equal((await output.next()).value, 'beat');
    fixture.stdin.write(`react ${reaction}\n`);
    assert.match((await output.next()).value, /^reacted \d+$/);
    return JSON.parse((await replies.next()).value).interpretation;
  };
  const stop = async () => {
    socket.destroy();
    fixture.stdin.end();
    assert.deepEqual(await exited, [0, null]);
  };
  t.after(() => {
    socket.destroy();
    if (fixture.exitCode === null) fixture.kill();
  });
  return { ask, beat, stop };
}

// #35's acceptance: the person's own picks change the next read once they
// beat the generic rule, for that reaction and that kind of beat only; they
// last across a restart; and a reset restores the generic read.
test('repeated picks for a frown at a question tune its next read until reset', { timeout: 30000 }, async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fp-tune-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const socketPath = path.join(dir, 's');
  const feelingsPath = path.join(dir, 'state', 'feelings');

  let service = await start(t, socketPath, feelingsPath);
  for (let pick = 0; pick < 3; ++pick) {
    const read = await service.beat('question', frown);
    assert.equal(read.feeling, 'confused', `pick ${pick}`);
    assert.deepEqual(await service.ask(`feel ${read.id} annoyance`), { ok: true });
  }
  assert.equal((await service.beat('question', frown)).feeling, 'annoyance');
  assert.equal((await service.beat('proposal', frown)).feeling, 'confused');
  assert.equal(
    await readFile(feelingsPath, 'utf8'),
    'facepalm feelings 2\nbrows_down question unknown annoyance 3\n',
  );
  assert.equal((await stat(feelingsPath)).mode & 0o777, 0o600);
  await service.stop();

  service = await start(t, socketPath, feelingsPath);
  assert.equal((await service.beat('question', frown)).feeling, 'annoyance');
  assert.deepEqual(await service.ask('reset feelings'), { ok: true });
  assert.equal((await service.beat('question', frown)).feeling, 'confused');
  assert.equal(await readFile(feelingsPath, 'utf8'), 'facepalm feelings 2\n');
  await service.stop();
});

test('a second service cannot open the same feelings file', { timeout: 10000 }, async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fp-tune-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const feelingsPath = path.join(dir, 'feelings');
  const service = await start(t, path.join(dir, 'a'), feelingsPath);

  const second = spawn(fixturePath, [path.join(dir, 'b'), feelingsPath]);
  const [code] = await once(second, 'exit');

  assert.equal(code, 1);
  await service.stop();
});
