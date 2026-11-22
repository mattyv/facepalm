import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js';
import { createServer } from '../server.js';
import { OfflineError, UnapprovedError } from '../session.js';

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
    assert.ok(tools.tools.find((tool) => tool.name === 'ask_feeling'));
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

test('a session that is not connected right now says Facepalm is off, for both tools (#117)', async () => {
  const session = { request: async () => { throw new OfflineError('offline'); } };
  const { server, client } = await connected(session);
  try {
    const waited = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.equal(waited.isError, true);
    assert.match(waited.content[0].text, /Facepalm is off/);
    const told = await client.callTool({ name: 'tell_feeling', arguments: { id: 1, feeling: 'confused' } });
    assert.equal(told.isError, true);
    assert.match(told.content[0].text, /Facepalm is off/);
  } finally { await client.close(); await server.close(); }
});

test('an unapproved session says so, not that Facepalm is off, for both tools (#129)', async () => {
  const session = { request: async () => { throw new UnapprovedError('unapproved'); } };
  const { server, client } = await connected(session);
  try {
    const waited = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.equal(waited.isError, true);
    assert.match(waited.content[0].text, /hasn't approved this client/);
    const told = await client.callTool({ name: 'tell_feeling', arguments: { id: 1, feeling: 'confused' } });
    assert.equal(told.isError, true);
    assert.match(told.content[0].text, /hasn't approved this client/);
  } finally { await client.close(); await server.close(); }
});

test('tell_feeling refuses an interpretation this session no longer remembers, unsent (#129)', async () => {
  const commands = [];
  const session = {
    request: async (command) => { commands.push(command); return { ok: true }; },
    knowsInterpretation: (id) => id === 12,
  };
  const { server, client } = await connected(session);
  try {
    const refused = await client.callTool({ name: 'tell_feeling', arguments: { id: 1, feeling: 'confused' } });
    assert.equal(refused.isError, true);
    assert.match(refused.content[0].text, /no longer recognizes/);
    const told = await client.callTool({ name: 'tell_feeling', arguments: { id: 12, feeling: 'confused' } });
    assert.equal(told.content[0].text, 'Feeling taken.');
    assert.deepEqual(commands, ['feel 12 confused']);
  } finally { await client.close(); await server.close(); }
});

test('wait_for_reaction records an interpretation id so tell_feeling can recognize it later (#129)', async () => {
  const noted = [];
  const session = {
    request: async () => ({
      readings: [], missed: 0,
      interpretation: { id: 7, feeling: 'agreement', beat: 'question', feelings: ['agreement', 'unknown'] },
    }),
    noteInterpretation: (id) => noted.push(id),
  };
  const { server, client } = await connected(session);
  try {
    await client.callTool({ name: 'wait_for_reaction', arguments: { beat: 'question' } });
    assert.deepEqual(noted, [7]);
  } finally { await client.close(); await server.close(); }
});

test('wait_for_reaction with no interpretation never calls noteInterpretation (#129)', async () => {
  const noted = [];
  const session = {
    request: async () => ({ readings: [], missed: 0 }),
    noteInterpretation: (id) => noted.push(id),
  };
  const { server, client } = await connected(session);
  try {
    await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.deepEqual(noted, []);
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

test('screen context round-trips through the delivery, window title and calibrated screen included', async () => {
  const context = { active_app: 'Finder', window_title: 'Desktop', mouse: { x: 12, y: 34 }, gaze: 'this_screen', calibrated_screen: 1 };
  const { server, client } = await connected({
    request: async () => ({ readings: [{ id: 1, summary: 'x', detail: {}, context }], missed: 0 }),
  });
  try {
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.deepEqual(JSON.parse(result.content[0].text).readings[0].context, context);
  } finally { await client.close(); await server.close(); }
});

test('screen context with no window title or calibrated screen round-trips too', async () => {
  const context = { mouse: { x: -5, y: 0 }, gaze: 'away' };
  const { server, client } = await connected({
    request: async () => ({ readings: [{ id: 1, summary: 'x', detail: {}, context }], missed: 0 }),
  });
  try {
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.deepEqual(JSON.parse(result.content[0].text).readings[0].context, context);
  } finally { await client.close(); await server.close(); }
});

test('a reading with no context omits it, same as today', async () => {
  const { server, client } = await connected({
    request: async () => ({ readings: [{ id: 1, summary: 'x', detail: {} }], missed: 0 }),
  });
  try {
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.equal('context' in JSON.parse(result.content[0].text).readings[0], false);
  } finally { await client.close(); await server.close(); }
});

test('a reading with a malformed context closes the session, same as a malformed detail', async () => {
  let closed = false;
  const { server, client } = await connected({
    request: async () => ({ readings: [{ id: 1, summary: 'x', detail: {}, context: { gaze: 4 } }], missed: 0 }),
    close: () => { closed = true; },
  });
  try {
    const result = await client.callTool({ name: 'wait_for_reaction', arguments: {} });
    assert.equal(result.isError, true);
    assert.equal(closed, true);
  } finally { await client.close(); await server.close(); }
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
