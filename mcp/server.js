import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { z } from 'zod';
import { registerFeelingCard } from './feeling-widget.js';

// A feeling's name. The names come from the service's one list
// (src/feelings.h), sent with each interpretation; the adapter checks only
// that a feeling is a name, and the service refuses one it does not know.
const feelingName = z.string().regex(/^[a-z]{1,32}$/);
const delivery = z.object({
  readings: z.array(z.object({ id: z.number().int().positive().safe(), summary: z.string(), published_at_ms: z.number().int().safe().optional(), detail: z.record(z.string(), z.unknown()) })),
  missed: z.number().int().nonnegative().safe(),
  interpretation: z.object({
    id: z.number().int().positive().safe(), feeling: feelingName, beat: z.enum(['question', 'proposal']),
    feelings: z.array(feelingName).min(1),
  }).optional(),
});

export function createServer(session) {
  const server = new McpServer({ name: 'facepalm', version: '0.1.0' });
  registerFeelingCard(server);
  server.registerTool('wait_for_reaction', {
    description: 'Wait for interpreted reactions. Show each summary verbatim with its reading ID; do not present detail as the summary. '
      + 'Right after asking the person a question or making a proposal, pass beat ("question" or "proposal"): Facepalm watches the next timeout_ms '
      + '(default 1500, at most 10000) and returns interpretation.feeling, its tentative read of how they felt about it, unknown when it cannot tell, '
      + 'and interpretation.feelings, every feeling the person can pick instead. '
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
  server.registerTool('tell_feeling', {
    description: 'Tell Facepalm how the person says they really felt at a beat, so it reads them better next time. '
      + 'Pass the interpretation id from wait_for_reaction and the feeling the person picked, one of that interpretation\'s feelings. '
      + 'Send only a feeling the person chose, never a guess. Facepalm takes one feeling per interpretation, within five minutes of the beat.',
    inputSchema: { id: z.number().int().positive().safe(), feeling: feelingName },
  }, async ({ id, feeling }, { signal }) => {
    try {
      const result = await session.request(`feel ${id} ${feeling}`, signal, 5000);
      if (result?.ok !== true) throw new Error('feeling refused');
      return { content: [{ type: 'text', text: 'Feeling taken.' }] };
    } catch { return { isError: true, content: [{ type: 'text', text: 'Facepalm did not take that feeling. It takes one per interpretation given to this session, within five minutes; restart the MCP server if the session was interrupted.' }] }; }
  });
  return server;
}
