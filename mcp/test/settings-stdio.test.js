import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

const adapter = process.env.FACEPALM_MCP_ADAPTER ?? fileURLToPath(new URL('../main.js', import.meta.url));

test('stdio advertises settings without launching it or connecting to the camera', async () => {
  const client = new Client({ name: 'settings-list-test', version: '1' });
  const transport = new StdioClientTransport({ command: process.execPath, args: [adapter, '/tmp/facepalm-settings-no-server.sock'] });
  try {
    await client.connect(transport);
    const tools = await client.listTools();
    const settings = tools.tools.find((tool) => tool.name === 'open_facepalm_settings');
    assert.ok(settings);
    assert.match(settings.description, /does not start the camera/);
  } finally { await client.close(); }
});
