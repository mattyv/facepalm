import { readFile } from 'node:fs/promises';
import { registerAppResource, registerAppTool, RESOURCE_MIME_TYPE } from '@modelcontextprotocol/ext-apps/server';
import { z } from 'zod';

// A feeling's name, as server.js checks it: the names come from the
// service's one list (src/feelings.h), sent with each interpretation.
const feelingName = z.string().regex(/^[a-z]{1,32}$/);

export const feelingCardUri = 'ui://facepalm/feeling-card.html';

// Registers the "pick the feeling" card (#61 item 5) as an MCP Apps widget:
// the resource it renders, and the tool that shows it. show_feeling_card
// takes the interpretation's id, feeling and feelings directly, the same
// shape wait_for_reaction already returned to the LLM, rather than an id
// alone: this server keeps no record of interpretations between calls (the
// native service already owns that, and times it out after five minutes),
// so there is nothing here to look an id up in. Keeping it a tool of its
// own, separate from wait_for_reaction, means a plain wait or a client with
// no beat never carries widget framing, and a host without MCP Apps still
// gets a usable fallback: the tool's own text content.
export function registerFeelingCard(server) {
  registerAppResource(server, 'Facepalm feeling card', feelingCardUri, {}, async (uri) => ({
    contents: [{
      uri: uri.href,
      mimeType: RESOURCE_MIME_TYPE,
      text: await readFile(new URL('./feeling-widget.html', import.meta.url), 'utf8'),
      _meta: { ui: { csp: { connectDomains: [], resourceDomains: [] }, prefersBorder: true } },
    }],
  }));
  registerAppTool(server, 'show_feeling_card', {
    description: 'Show the person a card to pick how they felt at one interpretation, when the host renders interactive widgets (MCP Apps). '
      + 'Pass id, feeling and feelings exactly as wait_for_reaction returned them for that beat. '
      + 'A click sends tell_feeling itself: the pick replaces the read for that beat, in tell_feeling\'s own result where the host passes it on, '
      + 'or otherwise found by calling get_feeling with the id. Advisory either way. '
      + 'A host without widget support shows this tool\'s own text instead: ask the person how they felt, in your own words, then call tell_feeling yourself.',
    inputSchema: { id: z.number().int().positive().safe(), feeling: feelingName, feelings: z.array(feelingName).min(1) },
    _meta: { ui: { resourceUri: feelingCardUri } },
  }, async ({ id, feeling, feelings }) => {
    if (!feelings.includes(feeling)) {
      return { isError: true, content: [{ type: 'text', text: 'feeling must be one of feelings.' }] };
    }
    return {
      content: [{ type: 'text', text: 'Ask the person how they were feeling just then, and call tell_feeling with their pick.' }],
      structuredContent: { id, feeling, feelings },
    };
  });
}
