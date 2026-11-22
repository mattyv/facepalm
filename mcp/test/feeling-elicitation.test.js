import { test } from 'node:test';
import assert from 'node:assert/strict';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js';
import { ElicitRequestSchema } from '@modelcontextprotocol/sdk/types.js';
import { registerFeelingElicitation } from '../feeling-elicitation.js';
import { OfflineError } from '../session.js';

const feelings = ['agreement', 'disagreement', 'engagement', 'annoyance', 'confused', 'unknown'];

async function connected(session, onElicit) {
  const server = new McpServer({ name: 'test', version: '1' });
  registerFeelingElicitation(server, session);
  const [a, b] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: 'test', version: '1' }, { capabilities: onElicit ? { elicitation: {} } : {} });
  if (onElicit) client.setRequestHandler(ElicitRequestSchema, onElicit);
  await Promise.all([server.connect(a), client.connect(b)]);
  return { server, client };
}

test('asks with a form of the given feelings, the read pre-selected, and takes an accepted pick', async () => {
  const commands = [];
  const session = { request: async (command) => { commands.push(command); return { ok: true }; } };
  let asked;
  const { server, client } = await connected(session, (request) => {
    asked = request.params;
    return { action: 'accept', content: { feeling: 'disagreement' } };
  });
  try {
    const tool = (await client.listTools()).tools.find((item) => item.name === 'ask_feeling');
    assert.ok(tool);
    const result = await client.callTool({ name: 'ask_feeling', arguments: { id: 7, feeling: 'confused', feelings } });
    assert.notEqual(result.isError, true);
    assert.equal(asked.message, 'How were you feeling just then?');
    const property = asked.requestedSchema.properties.feeling;
    assert.deepEqual(property.enum, feelings);
    assert.deepEqual(property.enumNames, ['Agreeing', 'Disagreeing', 'Interested', 'Annoyed', 'Confused', 'Not sure']);
    assert.equal(property.default, 'confused');
    assert.deepEqual(commands, ['feel 7 disagreement']);
  } finally { await client.close(); await server.close(); }
});

test('shows an unmapped feeling name as itself', async () => {
  const session = { request: async () => ({ ok: true }) };
  let asked;
  const { server, client } = await connected(session, (request) => {
    asked = request.params;
    return { action: 'accept', content: { feeling: 'novel' } };
  });
  try {
    await client.callTool({ name: 'ask_feeling', arguments: { id: 1, feeling: 'novel', feelings: ['novel'] } });
    assert.deepEqual(asked.requestedSchema.properties.feeling.enumNames, ['novel']);
  } finally { await client.close(); await server.close(); }
});

test('reports, without an error, that the person declined or cancelled', async () => {
  for (const action of ['decline', 'cancel']) {
    const session = { request: () => assert.fail('must not send a pick') };
    const { server, client } = await connected(session, () => ({ action }));
    try {
      const result = await client.callTool({ name: 'ask_feeling', arguments: { id: 1, feeling: 'confused', feelings } });
      assert.notEqual(result.isError, true);
      assert.match(result.content[0].text, /did not pick/);
    } finally { await client.close(); await server.close(); }
  }
});

test('a pick outside the requested schema is caught as a failed elicitation, before sending it', async () => {
  // elicitInput validates an "accept" response against requestedSchema
  // itself; one outside it (wrong type, or not in the enum) throws, the
  // same as a host that refuses to elicit at all.
  const session = { request: () => assert.fail('must not send a pick') };
  for (const content of [{ feeling: 'not-a-feeling' }, {}, { feeling: 3 }]) {
    const { server, client } = await connected(session, () => ({ action: 'accept', content }));
    try {
      const result = await client.callTool({ name: 'ask_feeling', arguments: { id: 1, feeling: 'confused', feelings } });
      assert.equal(result.isError, true);
      assert.match(result.content[0].text, /could not ask/i);
    } finally { await client.close(); await server.close(); }
  }
});

test('a refused or failed feeling, and an elicitation the host refuses, are errors', async () => {
  for (const session of [
    { request: async () => ({ ok: false }) },
    { request: async () => { throw new Error('/private/path'); } },
  ]) {
    const { server, client } = await connected(session, () => ({ action: 'accept', content: { feeling: 'confused' } }));
    try {
      const result = await client.callTool({ name: 'ask_feeling', arguments: { id: 1, feeling: 'confused', feelings } });
      assert.equal(result.isError, true);
      assert.doesNotMatch(JSON.stringify(result), /private/);
    } finally { await client.close(); await server.close(); }
  }
  const { server, client } = await connected({ request: () => assert.fail('must not send a pick') }, undefined);
  try {
    const result = await client.callTool({ name: 'ask_feeling', arguments: { id: 1, feeling: 'confused', feelings } });
    assert.equal(result.isError, true);
  } finally { await client.close(); await server.close(); }
});

test('a session that is not connected right now says Facepalm is off (#117)', async () => {
  const session = { request: async () => { throw new OfflineError('offline'); } };
  const { server, client } = await connected(session, () => ({ action: 'accept', content: { feeling: 'confused' } }));
  try {
    const result = await client.callTool({ name: 'ask_feeling', arguments: { id: 1, feeling: 'confused', feelings } });
    assert.equal(result.isError, true);
    assert.match(result.content[0].text, /Facepalm is off/);
  } finally { await client.close(); await server.close(); }
});

test('rejects a feeling that is not in the interpretation\'s own feelings list, before asking', async () => {
  const session = { request: () => assert.fail('must not send a pick') };
  let askedAtAll = false;
  const { server, client } = await connected(session, () => { askedAtAll = true; return { action: 'accept', content: {} }; });
  try {
    const result = await client.callTool({ name: 'ask_feeling', arguments: { id: 1, feeling: 'confused', feelings: ['agreement', 'unknown'] } });
    assert.equal(result.isError, true);
    assert.equal(askedAtAll, false);
  } finally { await client.close(); await server.close(); }
});

test('rejects malformed input', async () => {
  const session = { request: () => assert.fail('must not send a pick') };
  const { server, client } = await connected(session, () => assert.fail('must not ask'));
  try {
    for (const args of [
      { id: 0, feeling: 'confused', feelings },
      { id: 1, feeling: 'Confused!', feelings },
      { id: 1, feeling: 'confused', feelings: [] },
    ]) {
      const result = await client.callTool({ name: 'ask_feeling', arguments: args });
      assert.equal(result.isError, true);
    }
  } finally { await client.close(); await server.close(); }
});
