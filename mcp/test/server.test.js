import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js';
import { createServer } from '../server.js';

async function connected(session) {
  const server = createServer(session);
  const [a, b] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: 'test', version: '1' });
  await Promise.all([server.connect(a), client.connect(b)]);
  return { server, client };
}

test('tools validate inputs and preserve delivered summaries and details', async () => {
  const commands = [];
  const session = { request: async (command) => {
    commands.push(command);
    return command.startsWith('wait')
      ? { readings: [{ id: 12, summary: '  A person nodded.  ', detail: { gesture: 'nod' }, published_at_ms: 1763658000123 }], missed: 0 }
      : { ok: true };
  }};
  const { server, client } = await connected(session);
  try {
    const tools = await client.listTools();
    assert.match(tools.tools.find((tool) => tool.name === 'wait_for_reaction').description, /verbatim.*ID/);
    assert.match(tools.tools.find((tool) => tool.name === 'rate_reaction').description, /five minutes/);
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.deepEqual(JSON.parse(result.content[0].text), {
      readings: [{ id: 12, summary: '  A person nodded.  ', detail: { gesture: 'nod' }, published_at_ms: 1763658000123 }], missed: 0,
    });
    assert.deepEqual(commands, ['wait 5000']);
    const rating = await client.callTool({ name: 'rate_reaction', arguments: { id: 12, score: 5 } });
    assert.equal(rating.content[0].text, 'Rating accepted.');
    assert.deepEqual(commands, ['wait 5000', 'rate 12 5']);
    const invalid = await client.callTool({ name: 'rate_reaction', arguments: { id: 12, score: 6 } });
    assert.equal(invalid.isError, true);
    const tooLong = await client.callTool({ name: 'wait_for_reaction', arguments: { timeout_ms: 60001 } });
    assert.equal(tooLong.isError, true);
    assert.equal(commands.length, 2);
  } finally { await client.close(); await server.close(); }
});

test('native failure is sanitized', async () => {
  const { server, client } = await connected({ request: async () => { throw new Error('/private/path'); } });
  try {
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: { timeout_ms: 0 } });
    assert.equal(result.isError, true);
    assert.doesNotMatch(JSON.stringify(result), /private/);
  } finally { await client.close(); await server.close(); }
});

test('invalid delivery shape closes the session', async () => {
  let closed = false;
  const { server, client } = await connected({
    request: async () => ({ readings: [{ id: 1, summary: 'x', detail: 'raw' }], missed: 0 }),
    close: () => { closed = true; },
  });
  try {
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.equal(result.isError, true);
    assert.equal(closed, true);
  } finally { await client.close(); await server.close(); }
});

test('refused rating is an error', async () => {
  const { server, client } = await connected({ request: async () => ({ error: 'cannot rate that reading' }) });
  try {
    const result = await client.callTool({ name: 'rate_reaction', arguments: { id: 4, score: 3 } });
    assert.equal(result.isError, true);
  } finally { await client.close(); await server.close(); }
});
