import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const action = fileURLToPath(new URL('../hooks/action.mjs', import.meta.url));

const event = (extra = {}) => JSON.stringify({
  hook_event_name: 'PostToolUse', session_id: 'session-1', tool_use_id: 'tool-1', tool_name: 'Bash', ...extra,
});

// Stands in for the hook broker: a Unix socket that replies however the
// test wants, and records what action.mjs sent it.
async function broker(handler) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'facepalm-action-'));
  const address = path.join(dir, 'broker.sock');
  const received = [];
  const server = net.createServer((socket) => {
    socket.setEncoding('utf8');
    let buffer = '';
    let answered = false;
    socket.on('data', (chunk) => {
      buffer += chunk;
      const newline = buffer.indexOf('\n');
      if (newline < 0) return;
      received.push(buffer.slice(0, newline));
      buffer = buffer.slice(newline + 1);
      if (answered) return;
      answered = true;
      handler(socket);
    });
  });
  await new Promise((resolve) => server.listen(address, resolve));
  return { address, received, close: () => new Promise((resolve) => server.close(resolve)).then(() => rm(dir, { recursive: true, force: true })) };
}

// Runs action.mjs once with the given stdin and broker socket (or none),
// and returns its stdout and exit code.
async function run({ input = event(), socket, cwd } = {}) {
  const child = spawn(process.execPath, [action], {
    env: { ...process.env, ...(socket !== undefined ? { FACEPALM_HOOK_SOCKET: socket } : {}) },
    cwd,
  });
  const stdout = [];
  child.stdout.on('data', (chunk) => stdout.push(chunk));
  const exited = once(child, 'exit');
  if (input !== null) child.stdin.write(input);
  child.stdin.end();
  const [code] = await exited;
  return { code, stdout: Buffer.concat(stdout).toString() };
}

test('does nothing, and exits 0, with no hook socket configured', async () => {
  const result = await run({ socket: undefined });
  assert.deepEqual(result, { code: 0, stdout: '' });
});

test('exits 0 without connecting when stdin is not a valid hook event', async () => {
  const b = await broker(() => assert.fail('must not connect'));
  try {
    const result = await run({ input: 'not-json', socket: b.address });
    assert.deepEqual(result, { code: 0, stdout: '' });
    assert.deepEqual(b.received, []);
  } finally { await b.close(); }
});

test('exits 0 without reading the rest of oversized stdin', async () => {
  const b = await broker(() => assert.fail('must not connect'));
  try {
    const child = spawn(process.execPath, [action], { env: { ...process.env, FACEPALM_HOOK_SOCKET: b.address } });
    const exited = once(child, 'exit');
    child.stdin.write(Buffer.alloc(65537, 65));
    const [code] = await exited;
    assert.equal(code, 0);
  } finally { await b.close(); }
});

test('sends the parsed event, acks a reply with no context, and writes nothing', async () => {
  const b = await broker((socket) => socket.write('{"ok":true,"delivery":{"readings":[],"missed":0}}\n'));
  try {
    const result = await run({ socket: b.address });
    assert.equal(result.code, 0);
    assert.equal(result.stdout, '');
    assert.deepEqual(JSON.parse(b.received[0]), {
      event: 'PostToolUse', session: 'session-1', toolUse: 'tool-1', tool: 'Bash',
    });
    assert.equal(b.received[1], 'ack');
  } finally { await b.close(); }
});

test('writes the formatted context to stdout, then acks', async () => {
  const b = await broker((socket) => socket.write(`${JSON.stringify({
    ok: true, delivery: { readings: [{ id: 1, summary: 'A nod' }], missed: 0 },
  })}\n`));
  try {
    const result = await run({ socket: b.address });
    assert.equal(result.code, 0);
    assert.deepEqual(JSON.parse(result.stdout), {
      hookSpecificOutput: { hookEventName: 'PostToolUse', additionalContext: 'Facepalm: [1] A nod' },
    });
    assert.equal(b.received[1], 'ack');
  } finally { await b.close(); }
});

test('ends without acking a refused or malformed reply', async () => {
  for (const reply of ['{"ok":false}\n', '{"ok":true}\n', 'not-json\n']) {
    const b = await broker((socket) => socket.write(reply));
    try {
      const result = await run({ socket: b.address });
      assert.equal(result.code, 0);
      assert.equal(result.stdout, '');
      assert.equal(b.received.length, 1);
    } finally { await b.close(); }
  }
});

test('destroys the connection on an oversized or fragmented-forever reply', async () => {
  const b = await broker((socket) => socket.write('x'.repeat(65537)));
  try {
    const result = await run({ socket: b.address });
    assert.equal(result.code, 0);
    assert.equal(result.stdout, '');
  } finally { await b.close(); }
});

test('exits 0 when nothing is listening on the configured socket', async () => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'facepalm-action-'));
  try {
    const result = await run({ socket: path.join(dir, 'nothing.sock') });
    assert.deepEqual(result, { code: 0, stdout: '' });
  } finally { await rm(dir, { recursive: true, force: true }); }
});

test('destroys the connection when writing the context to stdout fails', async () => {
  const b = await broker((socket) => socket.write(`${JSON.stringify({
    ok: true, delivery: { readings: [{ id: 1, summary: 'A nod' }], missed: 0 },
  })}\n`));
  try {
    const child = spawn(process.execPath, [action], {
      env: { ...process.env, FACEPALM_HOOK_SOCKET: b.address },
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    const exited = once(child, 'exit');
    child.stdout.on('error', () => {});
    child.stdout.destroy();
    child.stdin.write(event());
    child.stdin.end();
    const [code] = await exited;
    assert.equal(code, 0);
  } finally { await b.close(); }
});

test('destroys the connection if the broker never answers within its timeout', { timeout: 10000 }, async () => {
  const b = await broker(() => {});
  try {
    const result = await run({ socket: b.address });
    assert.deepEqual(result, { code: 0, stdout: '' });
  } finally { await b.close(); }
});
