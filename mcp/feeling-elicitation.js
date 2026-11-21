import { z } from 'zod';

// A feeling's name, as server.js checks it: the names come from the
// service's one list (src/feelings.h), sent with each interpretation.
const feelingName = z.string().regex(/^[a-z]{1,32}$/);

// The same plain-word labels feeling-view.js (the MCP Apps card) uses, so
// the question reads the same whichever way a host answers it.
const labels = {
  agreement: 'Agreeing',
  disagreement: 'Disagreeing',
  engagement: 'Interested',
  annoyance: 'Annoyed',
  confused: 'Confused',
  unknown: 'Not sure',
};

// Registers ask_feeling (#61 item 6): the same "pick the feeling" question
// as the MCP Apps card (#61 item 5), asked instead through MCP elicitation,
// for a host that supports elicitation but not an inline widget, such as
// the Claude Code terminal. One form field, an enum of the interpretation's
// own feelings with plain-word names and Facepalm's read as the default,
// and an accepted pick is sent on as tell_feeling already is: feel <id>
// <feeling>. Unlike show_feeling_card, this tool sends the pick itself,
// since there is no separate view layer here to do it, the way the
// widget's own script does.
export function registerFeelingElicitation(server, session, options = {}) {
  const elicitInput = options.elicitInput ?? ((params, opts) => server.server.elicitInput(params, opts));
  server.registerTool('ask_feeling', {
    description: 'Ask the person how they were feeling just then, through a form, for one interpretation a beat returned. '
      + 'Use this where the host supports elicitation but cannot render show_feeling_card\'s inline card (for example, the Claude Code terminal). '
      + 'Pass id, feeling and feelings exactly as wait_for_reaction returned them. '
      + 'This tool sends the person\'s pick to Facepalm itself; do not also call tell_feeling for the same interpretation. '
      + 'A host with neither elicitation nor a widget: ask in your own words instead, then call tell_feeling yourself.',
    inputSchema: { id: z.number().int().positive().safe(), feeling: feelingName, feelings: z.array(feelingName).min(1) },
  }, async ({ id, feeling, feelings }, { signal }) => {
    if (!feelings.includes(feeling)) {
      return { isError: true, content: [{ type: 'text', text: 'feeling must be one of feelings.' }] };
    }
    let result;
    try {
      result = await elicitInput({
        message: 'How were you feeling just then?',
        requestedSchema: {
          type: 'object',
          properties: {
            feeling: {
              type: 'string',
              enum: feelings,
              enumNames: feelings.map((name) => labels[name] ?? name),
              default: feeling,
            },
          },
          required: ['feeling'],
        },
      }, { signal });
    } catch { return { isError: true, content: [{ type: 'text', text: 'Could not ask the person how they felt.' }] }; }
    if (result?.action !== 'accept') {
      return { content: [{ type: 'text', text: 'The person did not pick a feeling.' }] };
    }
    // elicitInput validates an accepted response against requestedSchema
    // itself (the configured JSON Schema validator), so result.content.feeling
    // is already one of feelings here; a mismatch throws from the try above.
    const picked = result.content.feeling;
    try {
      const reply = await session.request(`feel ${id} ${picked}`, signal, 5000);
      if (reply?.ok !== true) throw new Error('feeling refused');
      return { content: [{ type: 'text', text: 'Feeling taken.' }] };
    } catch {
      return { isError: true, content: [{ type: 'text', text: 'Facepalm did not take that feeling. It takes one per interpretation given to this session, within five minutes; restart the MCP server if the session was interrupted.' }] };
    }
  });
}
