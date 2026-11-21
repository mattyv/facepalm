import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js';
import { createServer } from '../server.js';

async function connected(session, options) {
  const server = createServer(session, options);
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
    assert.match(tools.tools.find((tool) => tool.name === 'tell_feeling').description, /five minutes/);
    assert.equal(tools.tools.some((tool) => tool.name === 'rate_reaction'), false);
    assert.ok(tools.tools.find((tool) => tool.name === 'show_feeling_card')._meta.ui.resourceUri);
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.deepEqual(JSON.parse(result.content[0].text), {
      readings: [{ id: 12, summary: '  A person nodded.  ', detail: { gesture: 'nod' }, published_at_ms: 1763658000123 }], missed: 0,
    });
    assert.deepEqual(commands, ['wait 5000']);
    const told = await client.callTool({ name: 'tell_feeling', arguments: { id: 12, feeling: 'annoyance' } });
    assert.equal(told.content[0].text, 'Feeling taken.');
    assert.deepEqual(commands, ['wait 5000', 'feel 12 annoyance']);
    for (const args of [{ id: 12, feeling: 'Annoyed!' }, { id: 0, feeling: 'annoyance' }, { id: 12 }]) {
      assert.equal((await client.callTool({ name: 'tell_feeling', arguments: args })).isError, true);
    }
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

test('a refused or failed feeling is an error', async () => {
  for (const request of [async () => ({ error: 'cannot take that feeling' }), async () => { throw new Error('/private/path'); }]) {
    const { server, client } = await connected({ request });
    try {
      const result = await client.callTool({ name: 'tell_feeling', arguments: { id: 4, feeling: 'confused' } });
      assert.equal(result.isError, true);
      assert.match(result.content[0].text, /did not take that feeling/);
      assert.doesNotMatch(JSON.stringify(result), /private/);
    } finally { await client.close(); await server.close(); }
  }
});

test('a beat marks the window after a question and returns its interpretation', async () => {
  const commands = [];
  const reply = {
    readings: [{ id: 3, summary: 'Gesture: nod', detail: { gesture: 'nod' } }], missed: 0,
    interpretation: {
      id: 1, feeling: 'agreement', beat: 'question',
      feelings: ['agreement', 'disagreement', 'engagement', 'annoyance', 'confused', 'unknown'],
    },
  };
  const { server, client } = await connected({ request: async (command, signal, timeout) => {
    commands.push([command, timeout]);
    return reply;
  }});
  try {
    const description = (await client.listTools()).tools.find((tool) => tool.name === 'wait_for_reaction').description;
    assert.match(description, /beat/);
    assert.match(description, /never.*answer/i);
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: { beat: 'question' } });
    assert.deepEqual(JSON.parse(result.content[0].text), reply);
    const proposal = await client.callTool({ name: 'wait_for_reaction', arguments: { beat: 'proposal', timeout_ms: 10000 } });
    assert.notEqual(proposal.isError, true);
    assert.deepEqual(commands, [['beat question 1500', 6500], ['beat proposal 10000', 15000]]);
  } finally { await client.close(); await server.close(); }
});

test('a beat outside 1 to 10000 milliseconds or of another kind is refused unsent', async () => {
  const commands = [];
  const { server, client } = await connected({ request: async (command) => { commands.push(command); return {}; } });
  try {
    for (const args of [{ beat: 'question', timeout_ms: 0 }, { beat: 'question', timeout_ms: 10001 }]) {
      const result = await client.callTool({ name: 'wait_for_reaction', arguments: args });
      assert.equal(result.isError, true);
      assert.equal(result.content[0].text, 'A beat lasts 1 to 10000 milliseconds.');
    }
    assert.equal((await client.callTool({ name: 'wait_for_reaction', arguments: { beat: 'answer' } })).isError, true);
    assert.deepEqual(commands, []);
  } finally { await client.close(); await server.close(); }
});

test('a beat reply without an interpretation closes the session', async () => {
  const feelings = ['agreement', 'unknown'];
  for (const interpretation of [
    undefined, { id: 1, feeling: 'Agreement!', beat: 'question', feelings }, { id: 0, feeling: 'agreement', beat: 'question', feelings },
    { id: 1, feeling: 'agreement', beat: 'question' }, { id: 1, feeling: 'agreement', beat: 'question', feelings: [] },
  ]) {
    let closed = false;
    const { server, client } = await connected({
      request: async () => ({ readings: [], missed: 0, interpretation }),
      close: () => { closed = true; },
    });
    try {
      const result = await client.callTool({ name: 'wait_for_reaction', arguments: { beat: 'question' } });
      assert.equal(result.isError, true);
      assert.equal(closed, true);
    } finally { await client.close(); await server.close(); }
  }
});

test('settings tool requests only a launch and sanitizes failures', async () => {
  let calls = 0;
  const session = { request: async () => { throw new Error('camera should not be touched'); } };
  const { server, client } = await connected(session, {
    openSettings: async () => {
      calls++;
      if (calls === 2) throw new Error('/private/secret');
    },
  });
  try {
    assert.ok((await client.listTools()).tools.some((tool) => tool.name === 'open_facepalm_settings'));
    const opened = await client.callTool({ name: 'open_facepalm_settings', arguments: {} });
    assert.match(opened.content[0].text, /requested/i);
    assert.doesNotMatch(opened.content[0].text, /visible/i);
    const failed = await client.callTool({ name: 'open_facepalm_settings', arguments: {} });
    assert.equal(failed.isError, true);
    assert.doesNotMatch(JSON.stringify(failed), /private|secret/);
    assert.equal(calls, 2);
  } finally { await client.close(); await server.close(); }
});
