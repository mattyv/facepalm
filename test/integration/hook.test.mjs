import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createInterface } from 'node:readline';
import test from 'node:test';

const fixturePath = process.env.MCP_FIXTURE ?? './build/mcp_service_fixture';
const adapter = process.env.FACEPALM_MCP_ADAPTER ?? 'mcp/main.js';
const hook = 'integrations/claude-code/hooks/action.mjs';

test('hook and MCP share the real native feed cursor', { timeout: 15000 }, async (t) => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'fph-'));
  const nativePath = path.join(dir, 'native');
  const hookPath = path.join(dir, 'hook');
  const fixture = spawn(fixturePath, [nativePath, path.join(dir, 'feelings')]);
  const fixtureExited = once(fixture, 'exit');
  const fixtureLines = createInterface({ input: fixture.stdout })[Symbol.asyncIterator]();
  let mcp;
  let mcpExited;
  t.after(async () => {
    if (mcp && mcp.exitCode === null) mcp.kill();
    if (fixture.exitCode === null) fixture.kill();
    await Promise.all([fixtureExited, ...(mcpExited ? [mcpExited] : [])]);
    await rm(dir, { recursive: true, force: true });
  });
  assert.equal((await fixtureLines.next()).value, 'ready');
  mcp = spawn(process.execPath, [adapter, nativePath], {
    env: { ...process.env, FACEPALM_HOOK_SOCKET: hookPath },
  });
  mcpExited = once(mcp, 'exit');
  const replies = createInterface({ input: mcp.stdout })[Symbol.asyncIterator]();
  let id = 0;
  async function call(method, params) {
    const next = ++id;
    mcp.stdin.write(`${JSON.stringify({ jsonrpc: '2.0', id: next, method, params })}\n`);
    const reply = JSON.parse((await replies.next()).value);
    assert.equal(reply.id, next);
    assert.equal(reply.error, undefined);
    return reply.result;
  }
  await call('initialize', { protocolVersion: '2024-11-05', capabilities: {},
    clientInfo: { name: 'integration-test', version: '1' } });
  mcp.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n');
  const wait = async () => JSON.parse((await call('tools/call', {
    name: 'wait_for_reaction', arguments: { timeout_ms: 0 },
  })).content[0].text);
  assert.deepEqual((await wait()).readings, []);
  async function runHook(event) {
    const child = spawn(process.execPath, [hook], { env: { ...process.env, FACEPALM_HOOK_SOCKET: hookPath } });
    let output = '';
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', (chunk) => { output += chunk; });
    const exited = once(child, 'exit');
    child.stdin.end(JSON.stringify({ hook_event_name: event, session_id: 'session-1',
      tool_use_id: 'tool-1', tool_name: 'Bash', tool_input: { command: 'secret' } }));
    assert.deepEqual(await exited, [0, null]);
    return output;
  }
  assert.equal(await runHook('PreToolUse'), '');
  fixture.stdin.write('react\n');
  assert.equal((await fixtureLines.next()).value, 'reacted 1');
  const output = await runHook('PostToolUse');
  assert.match(output, /synthetic nod/);
  assert.ok(!output.includes('secret'));
  assert.deepEqual((await wait()).readings, []);
  mcp.stdin.end();
  assert.deepEqual(await mcpExited, [0, null]);
  fixture.stdin.end();
  assert.deepEqual(await fixtureExited, [0, null]);
});
