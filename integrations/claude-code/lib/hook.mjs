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

export function formatHookContext(event, delivery, currentTool) {
  if (!postEvents.has(event) || !Array.isArray(delivery?.readings) ||
      !Number.isSafeInteger(delivery.missed) || delivery.missed < 0 ||
      (delivery.omitted !== undefined && (!Number.isSafeInteger(delivery.omitted) || delivery.omitted < 0))) return null;
  const parts = [];
  for (const reading of delivery.readings.slice(0, 10)) {
    if (!Number.isSafeInteger(reading?.id) || reading.id <= 0 ||
        typeof reading.summary !== 'string') return null;
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
  if (!parts.length) return null;
  return { hookSpecificOutput: { hookEventName: event, additionalContext: `Facepalm: ${parts.join(' · ')}` } };
}
