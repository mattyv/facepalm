import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { z } from 'zod';

const delivery = z.object({
  readings: z.array(z.object({ id: z.number().int().positive().safe(), summary: z.string(), published_at_ms: z.number().int().safe().optional(), detail: z.record(z.string(), z.unknown()) })),
  missed: z.number().int().nonnegative().safe(),
});

export function createServer(session) {
  const server = new McpServer({ name: 'facepalm', version: '0.1.0' });
  server.registerTool('wait_for_reaction', {
    description: 'Wait for interpreted reactions. Show each summary verbatim with its reading ID; do not present detail as the summary.',
    inputSchema: { timeout_ms: z.number().int().min(0).max(60000).optional() },
  }, async ({ timeout_ms = 5000 }, { signal }) => {
    try {
      const response = await session.request(`wait ${timeout_ms}`, signal, timeout_ms + 5000);
      const parsed = delivery.safeParse(response);
      if (!parsed.success) { session.close?.(); throw new Error('invalid delivery'); }
      const result = parsed.data;
      return { content: [{ type: 'text', text: JSON.stringify(result) }] };
    } catch { return { isError: true, content: [{ type: 'text', text: 'Reaction service unavailable. Restart the MCP server if the session was interrupted.' }] }; }
  });
  server.registerTool('rate_reaction', {
    description: 'Rate a reaction delivered to this session, from 1 to 5, within five minutes of delivery, using its exact reading ID.',
    inputSchema: { id: z.number().int().positive().safe(), score: z.number().int().min(1).max(5) },
  }, async ({ id, score }, { signal }) => {
    try {
      const result = await session.request(`rate ${id} ${score}`, signal, 5000);
      if (result?.ok !== true) throw new Error('rating refused');
      return { content: [{ type: 'text', text: 'Rating accepted.' }] };
    } catch { return { isError: true, content: [{ type: 'text', text: 'Could not rate reaction. Restart the MCP server if the session was interrupted.' }] }; }
  });
  return server;
}
