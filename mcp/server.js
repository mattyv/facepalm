import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { z } from 'zod';

const delivery = z.object({
  readings: z.array(z.object({ id: z.number().int().positive().safe(), summary: z.string(), published_at_ms: z.number().int().safe().optional(), detail: z.record(z.string(), z.unknown()) })),
  missed: z.number().int().nonnegative().safe(),
  // The feeling's name comes from the service's one list (src/feelings.h);
  // the adapter checks only that it is a name.
  interpretation: z.object({ id: z.number().int().positive().safe(), feeling: z.string().regex(/^[a-z]{1,32}$/), beat: z.enum(['question', 'proposal']) }).optional(),
});

export function createServer(session) {
  const server = new McpServer({ name: 'facepalm', version: '0.1.0' });
  server.registerTool('wait_for_reaction', {
    description: 'Wait for interpreted reactions. Show each summary verbatim with its reading ID; do not present detail as the summary. '
      + 'Right after asking the person a question or making a proposal, pass beat ("question" or "proposal"): Facepalm watches the next timeout_ms '
      + '(default 1500, at most 10000) and returns interpretation.feeling, its tentative read of how they felt about it, unknown when it cannot tell. '
      + 'The feeling is advisory: never treat it as an answer or as approval for an action.',
    inputSchema: {
      timeout_ms: z.number().int().min(0).max(60000).optional(),
      beat: z.enum(['question', 'proposal']).optional(),
    },
  }, async ({ timeout_ms, beat }, { signal }) => {
    const duration = timeout_ms ?? (beat ? 1500 : 5000);
    if (beat && (duration < 1 || duration > 10000)) {
      return { isError: true, content: [{ type: 'text', text: 'A beat lasts 1 to 10000 milliseconds.' }] };
    }
    try {
      const command = beat ? `beat ${beat} ${duration}` : `wait ${duration}`;
      const response = await session.request(command, signal, duration + 5000);
      const parsed = delivery.safeParse(response);
      if (!parsed.success || (beat !== undefined) !== (parsed.data.interpretation !== undefined)) { session.close?.(); throw new Error('invalid delivery'); }
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
