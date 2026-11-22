import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { z } from 'zod';
import { registerFeelingCard } from './feeling-widget.js';
import { registerFeelingElicitation } from './feeling-elicitation.js';
import { feelingResult } from './feeling-result.js';
import { OfflineError, UnapprovedError } from './session.js';

// What a tool says when session.request() rejects because this session is
// not connected right now (#117): the session itself keeps retrying with
// a bounded backoff, so this no longer tells anyone to restart the MCP
// server, unlike the message this replaced.
const offlineText = 'Facepalm is off; turn it on from its menu.';
// What a tool says when the daemon's hello refused this client's own
// token (#129), told apart from offlineText so the person is pointed at
// Facepalm Settings, not at turning Facepalm on, which may already be on.
const unapprovedText = "Facepalm hasn't approved this client; turn Facepalm on from its menu, or run: facepalm-daemon approve mcp";
// What tell_feeling and ask_feeling say for an interpretation id this
// session no longer remembers (#129): forgotten on its last reconnect,
// since src/reaction_feed.cc's own interpretation ids restart at 1 on
// every daemon run, so an id from before one could now mean an entirely
// different beat.
const unknownInterpretationText = 'Facepalm no longer recognizes that interpretation; it may be from before Facepalm last restarted.';
// Appended to offlineText, unapprovedText, unknownInterpretationText and a
// native failure's own text in tell_feeling (#133), for a call that never
// reached a definite yes or no from Facepalm: the agent already has
// whatever read wait_for_reaction gave it for this id, and nothing here
// overrides it, unlike a pick Facepalm did get to answer, yes or no, which
// tell_feeling reports with the fuller {id, read, picked, feeling} shape
// instead (feeling-result.js).
const readStandsText = " Facepalm's read stands.";

// A feeling's name. The names come from the service's one list
// (src/feelings.h), sent with each interpretation; the adapter checks only
// that a feeling is a name, and the service refuses one it does not know.
const feelingName = z.string().regex(/^[a-z]{1,32}$/);
// A reading's screen context (#36, #92): active_app and window_title only
// when the daemon has one to report (window_title only when the person
// opted in, which the daemon alone decides), mouse only off a headless
// session, gaze always, and calibrated_screen only once a profile exists
// (#116). src/screen_context_reporter.cc's Json is the oracle for this
// shape.
const context = z.object({
  active_app: z.string().optional(),
  window_title: z.string().optional(),
  mouse: z.object({ x: z.number(), y: z.number() }).optional(),
  gaze: z.string(),
  calibrated_screen: z.number().int().nonnegative().safe().optional(),
});
// The majority coarse gaze over a beat's own window (#142): on_content
// (the one screen a beat is about), averted (elsewhere, but not fully
// away), away or unknown -- context only, never a rule's input for
// feeling, the one exception being a question beat ending averted, which
// reads unknown instead. content (#144) is only present when the call
// gave a content_rect: whether gaze was mostly inside it, coarse only
// (yes, no or unknown), never angles or window titles.
const delivery = z.object({
  readings: z.array(z.object({
    id: z.number().int().positive().safe(), summary: z.string(), published_at_ms: z.number().int().safe().optional(),
    detail: z.record(z.string(), z.unknown()), context: context.optional(),
  })),
  missed: z.number().int().nonnegative().safe(),
  interpretation: z.object({
    id: z.number().int().positive().safe(), feeling: feelingName, beat: z.enum(['question', 'proposal']),
    gaze: z.enum(['on_content', 'averted', 'away', 'unknown']),
    content: z.enum(['yes', 'no', 'unknown']).optional(),
    feelings: z.array(feelingName).min(1),
  }).optional(),
});
// A content rectangle (#144): screen coordinates in whatever space the
// daemon's own screen source reports, consistent across one call, not
// across platforms. Only meaningful alongside beat.
const contentRect = z.object({
  x: z.number().int().safe(), y: z.number().int().safe(),
  width: z.number().int().min(1).safe(), height: z.number().int().min(1).safe(),
});

// `options.openSettings` is mcp/settings-launcher.js's openFacepalmSettings
// (main.js supplies the real one); a unit test supplies a fake instead, so
// this file never itself decides whether to use the real one.
export function createServer(session, options = {}) {
  const server = new McpServer({ name: 'facepalm', version: '0.1.0' });
  server.registerTool('open_facepalm_settings', {
    description: 'Open the installed Facepalm Settings window. This does not start the camera or change consent, and does not confirm the window became visible.',
    inputSchema: {},
  }, async () => {
    try {
      await options.openSettings();
      return { content: [{ type: 'text', text: 'Opening Facepalm Settings was requested.' }] };
    } catch { return { isError: true, content: [{ type: 'text', text: 'Facepalm Settings is unavailable or could not be opened.' }] }; }
  });
  registerFeelingCard(server);
  registerFeelingElicitation(server, session);
  server.registerTool('wait_for_reaction', {
    description: 'Wait for interpreted reactions. Show each summary verbatim with its reading ID; do not present detail as the summary. '
      + 'Right after asking the person a question or making a proposal, pass beat ("question" or "proposal"): Facepalm watches the next timeout_ms '
      + '(default 1500, at most 10000) and returns interpretation.feeling, its tentative read of how they felt about it, unknown when it cannot tell, '
      + 'and interpretation.feelings, every feeling the person can pick instead. interpretation.gaze is context, where they were mostly looking over '
      + 'that window (on_content, averted, away or unknown): it never changes feeling by rule, except that a question ending averted reads unknown, '
      + 'so the person had not answered yet. Pass content_rect, the screen area the question or proposal occupies, to also get interpretation.content '
      + '(yes, no or unknown): whether gaze was mostly inside it, coarse only. '
      + 'A later pick, through ask_feeling, show_feeling_card or tell_feeling, replaces this read for that beat; '
      + 'call get_feeling with the id to find out which one stands now. '
      + 'The feeling is advisory either way: never treat it as an answer or as approval for an action.',
    inputSchema: {
      timeout_ms: z.number().int().min(0).max(60000).optional(),
      beat: z.enum(['question', 'proposal']).optional(),
      content_rect: contentRect.optional(),
    },
  }, async ({ timeout_ms, beat, content_rect }, { signal }) => {
    const duration = timeout_ms ?? (beat ? 1500 : 5000);
    if (beat && (duration < 1 || duration > 10000)) {
      return { isError: true, content: [{ type: 'text', text: 'A beat lasts 1 to 10000 milliseconds.' }] };
    }
    if (content_rect && !beat) {
      return { isError: true, content: [{ type: 'text', text: 'content_rect only means something with a beat.' }] };
    }
    try {
      const rect = content_rect ? ` ${content_rect.x} ${content_rect.y} ${content_rect.width} ${content_rect.height}` : '';
      const command = beat ? `beat ${beat} ${duration}${rect}` : `wait ${duration}`;
      const response = await session.request(command, signal, duration + 5000);
      const parsed = delivery.safeParse(response);
      if (!parsed.success || (beat !== undefined) !== (parsed.data.interpretation !== undefined)) { session.close?.(); throw new Error('invalid delivery'); }
      const result = parsed.data;
      // #129: remembered so tell_feeling/ask_feeling can tell this id
      // apart from a stale one once this session reconnects; the read
      // goes with it (#133) so a later pick can be reported alongside it.
      if (result.interpretation) session.noteInterpretation?.(result.interpretation.id, result.interpretation.feeling);
      return { content: [{ type: 'text', text: JSON.stringify(result) }] };
    } catch (error) {
      const text = error instanceof UnapprovedError ? unapprovedText
        : error instanceof OfflineError ? offlineText : 'Reaction service unavailable.';
      return { isError: true, content: [{ type: 'text', text }] };
    }
  });
  server.registerTool('tell_feeling', {
    description: 'Tell Facepalm how the person says they really felt at a beat, so it reads them better next time. '
      + 'Pass the interpretation id from wait_for_reaction and the feeling the person picked, one of that interpretation\'s feelings. '
      + 'Send only a feeling the person chose, never a guess. Facepalm takes one feeling per interpretation, within five minutes of the beat. '
      + 'Returns id, read, picked and feeling (the pick, once Facepalm has taken it, otherwise the read): the pick replaces the read for that beat, advisory either way.',
    inputSchema: { id: z.number().int().positive().safe(), feeling: feelingName },
  }, async ({ id, feeling }, { signal }) => {
    // #129: refused locally, never sent, for an id this session no
    // longer remembers giving out.
    if (session.knowsInterpretation?.(id) === false) {
      return { isError: true, content: [{ type: 'text', text: unknownInterpretationText + readStandsText }] };
    }
    const read = session.interpretation?.(id)?.read;
    try {
      const result = await session.request(`feel ${id} ${feeling}`, signal, 5000);
      if (result?.ok !== true) {
        // Facepalm itself said no (already given, or too old): the
        // person did pick, so the pick still replaces the read for this
        // beat (#133), even though it will not tune from it.
        return feelingResult({
          id, read, picked: feeling, isError: true,
          text: 'Facepalm did not take that feeling; it will not tune from this pick.',
        });
      }
      session.recordPick?.(id, feeling);
      return feelingResult({ id, read, picked: feeling, text: 'Feeling taken.' });
    } catch (error) {
      // The exchange itself never reached a definite yes or no (offline,
      // unapproved, or some other native failure): unlike Facepalm
      // explicitly saying no above, there is no confirmed pick to report,
      // so the read stands, exactly as it already did (#133).
      const text = error instanceof UnapprovedError ? unapprovedText
        : error instanceof OfflineError
          ? offlineText
          : 'Facepalm did not take that feeling. It takes one per interpretation given to this session, within five minutes.';
      return { isError: true, content: [{ type: 'text', text: text + readStandsText }] };
    }
  });
  server.registerTool('get_feeling', {
    description: 'Look up which feeling now stands for one interpretation a beat returned: the person\'s own pick, once Facepalm has taken one, '
      + 'otherwise Facepalm\'s own read. Use this when a host does not surface ask_feeling\'s, show_feeling_card\'s or tell_feeling\'s own result back to you. '
      + 'Pass the interpretation id. Advisory either way, never an answer or an approval.',
    inputSchema: { id: z.number().int().positive().safe() },
  }, async ({ id }) => {
    const entry = session.interpretation?.(id);
    if (entry === undefined) {
      return { isError: true, content: [{ type: 'text', text: unknownInterpretationText }] };
    }
    return feelingResult({
      id, read: entry.read, picked: entry.picked,
      text: entry.picked !== undefined ? 'The person picked this feeling.' : "Facepalm's read stands; the person has not picked one.",
    });
  });
  return server;
}
