// Only validated action markers and interpreted summaries cross the hook boundary.
const events = new Set(['PreToolUse', 'PostToolUse', 'PostToolUseFailure']);
const postEvents = new Set(['PostToolUse', 'PostToolUseFailure']);
const tools = new Set(['Bash', 'Read', 'Write', 'Edit', 'Glob', 'Grep']);
const identifier = /^[A-Za-z0-9_-]{1,128}$/;

export function parseHookInput(input) {
  if (typeof input !== 'string' || Buffer.byteLength(input, 'utf8') > 65536) return null;
  let value;
  try { value = JSON.parse(input); } catch { return null; }
  if (!value || !events.has(value.hook_event_name) ||
      typeof value.session_id !== 'string' || !identifier.test(value.session_id) ||
      typeof value.tool_use_id !== 'string' || !identifier.test(value.tool_use_id) ||
      typeof value.tool_name !== 'string') return null;
  return {
    event: value.hook_event_name,
    session: value.session_id,
    toolUse: value.tool_use_id,
    tool: tools.has(value.tool_name) ? value.tool_name : 'Other',
  };
}

// One reading's validity, named so formatHookContext's guard below can
// fold it in with .some() rather than its own early return (see that
// guard's comment).
function invalidReading(reading) {
  return !Number.isSafeInteger(reading?.id) || reading.id <= 0 || typeof reading.summary !== 'string';
}

export function formatHookContext(event, delivery, currentTool) {
  // The reading check is folded into this same guard, rather than its own
  // "if (...) return null;": on its own, in any position, V8's precise
  // coverage never gave that exact shape a branch entry at all, taken or
  // not, so the gate in scripts/coverage_js.sh could not see it either
  // way; as one more clause of this guard, which it already tracks, it
  // does not have that problem.
  if (!postEvents.has(event) || !Array.isArray(delivery?.readings) ||
      !Number.isSafeInteger(delivery.missed) || delivery.missed < 0 ||
      (delivery.omitted !== undefined && (!Number.isSafeInteger(delivery.omitted) || delivery.omitted < 0)) ||
      delivery.readings.slice(0, 10).some(invalidReading)) return null;
  const parts = [];
  const readings = delivery.readings.slice(0, 10);
  for (const reading of readings) {
    const clean = reading.summary.replace(/[\x00-\x1f\x7f]+/g, ' ');
    const summary = clean.length > 160 ? `${clean.slice(0, 159)}…` : clean;
    let timing = '';
    if (delivery.window) {
      const { start, end } = delivery.window;
      if (!Number.isSafeInteger(reading.published_at_ms) || !Number.isSafeInteger(start) ||
          !Number.isSafeInteger(end)) timing = ' (publication time unknown)';
      else if (reading.published_at_ms >= start && reading.published_at_ms <= end)
        timing = delivery.window.toolUse && currentTool && delivery.window.toolUse !== currentTool
          ? ' (published during an earlier action)' : ' (published during action)';
      else timing = ' (published outside action)';
    }
    parts.push(`[${reading.id}] ${summary}${timing}`);
  }
  const omitted = Math.max(0, delivery.readings.length - 10) + (delivery.omitted ?? 0);
  if (omitted > 0) parts.push(`${omitted} more reading${omitted === 1 ? '' : 's'} omitted`);
  if (delivery.missed) parts.push(`${delivery.missed} readings missed`);
  return parts.length
    ? { hookSpecificOutput: { hookEventName: event, additionalContext: `Facepalm: ${parts.join(' · ')}` } }
    : null;
}
