// The one result shape ask_feeling, tell_feeling and get_feeling all
// return to the agent (#133): `read` is Facepalm's own tentative read of
// the beat (wait_for_reaction's own interpretation.feeling), `picked` is
// the feeling the person actually chose, once there is one, and
// `feeling` is `picked ?? read` -- the pick replaces the read for that
// beat; with no pick, the read stands. This is advisory either way, never
// an answer or an approval.
//
// A result built before either value could even be looked up (an id this
// session no longer recognizes, or a request that never reached Facepalm
// at all) carries no structuredContent, the same plain text these tools
// already gave; only a result with a read, a pick, or both carries it.
export function feelingResult({ id, read, picked, text, isError = false }) {
  const result = { content: [{ type: 'text', text }] };
  if (isError) result.isError = true;
  if (read !== undefined || picked !== undefined) {
    result.structuredContent = { id, read, picked, feeling: picked ?? read };
  }
  return result;
}
