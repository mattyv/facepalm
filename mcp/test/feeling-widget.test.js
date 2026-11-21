import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js';
import { registerFeelingCard, feelingCardUri } from '../feeling-widget.js';

async function connected() {
  const server = new McpServer({ name: 'test', version: '1' });
  registerFeelingCard(server);
  const [a, b] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: 'test', version: '1' });
  await Promise.all([server.connect(a), client.connect(b)]);
  return { server, client };
}

const feelings = ['agreement', 'disagreement', 'engagement', 'annoyance', 'confused', 'unknown'];

test('registers a tool pointing at the card resource, text fallback included', async () => {
  const { server, client } = await connected();
  try {
    const tools = await client.listTools();
    const tool = tools.tools.find((item) => item.name === 'show_feeling_card');
    assert.equal(tool._meta.ui.resourceUri, feelingCardUri);
    const result = await client.callTool({ name: 'show_feeling_card', arguments: { id: 7, feeling: 'confused', feelings } });
    assert.notEqual(result.isError, true);
    assert.ok(result.content[0].text.length > 0);
    assert.deepEqual(result.structuredContent, { id: 7, feeling: 'confused', feelings });
  } finally { await client.close(); await server.close(); }
});

test('serves the bundled card as the MCP Apps resource, minimal CSP, no network', async () => {
  const { server, client } = await connected();
  try {
    const resources = await client.listResources();
    assert.ok(resources.resources.some((item) => item.uri === feelingCardUri));
    const read = await client.readResource({ uri: feelingCardUri });
    const bundle = await readFile(new URL('../feeling-widget.html', import.meta.url), 'utf8');
    assert.equal(read.contents[0].text, bundle);
    assert.equal(read.contents[0].mimeType, 'text/html;profile=mcp-app');
    assert.deepEqual(read.contents[0]._meta.ui.csp, { connectDomains: [], resourceDomains: [] });
  } finally { await client.close(); await server.close(); }
});

test('refuses a feeling that is not in the interpretation\'s own feelings list', async () => {
  const { server, client } = await connected();
  try {
    const result = await client.callTool({ name: 'show_feeling_card', arguments: { id: 7, feeling: 'confused', feelings: ['agreement', 'unknown'] } });
    assert.equal(result.isError, true);
  } finally { await client.close(); await server.close(); }
});

test('rejects malformed input before it reaches the card', async () => {
  const { server, client } = await connected();
  try {
    for (const args of [
      { id: 0, feeling: 'confused', feelings },
      { id: 7, feeling: 'Confused!', feelings },
      { id: 7, feeling: 'confused', feelings: [] },
      { id: 7, feeling: 'confused', feelings: 'confused' },
    ]) {
      const result = await client.callTool({ name: 'show_feeling_card', arguments: args });
      assert.equal(result.isError, true);
    }
  } finally { await client.close(); await server.close(); }
});
