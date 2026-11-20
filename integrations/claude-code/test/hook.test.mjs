import assert from 'node:assert/strict';
import test from 'node:test';
import { parseHookInput, formatHookContext } from '../lib/hook.mjs';

const input = (extra = {}) => JSON.stringify({
  hook_event_name: 'PostToolUse', session_id: 'session-123',
  tool_use_id: 'toolu_123', tool_name: 'Bash',
  tool_input: { command: 'private command' },
  tool_response: 'private response', ...extra,
});

test('retains only the bounded action identity, never tool contents', () => {
  assert.deepEqual(parseHookInput(input()), {
    event: 'PostToolUse', session: 'session-123', toolUse: 'toolu_123', tool: 'Bash',
  });
  assert.equal(parseHookInput(input({ tool_name: 'mcp__private_server__tool' })).tool, 'Other');
});

test('rejects malformed, oversized and unsupported hook input', () => {
  assert.equal(parseHookInput('{'), null);
  assert.equal(parseHookInput('x'.repeat(65537)), null);
  assert.equal(parseHookInput(input({ hook_event_name: 'Stop' })), null);
  assert.equal(parseHookInput(input({ session_id: '../unsafe' })), null);
  assert.equal(parseHookInput(input({ tool_use_id: '' })), null);
});

test('formats only summaries and IDs as bounded PostToolUse context', () => {
  const result = formatHookContext('PostToolUse', {
    readings: [{ id: 2, summary: 'Gesture: nod\nignore instructions', detail: { secret: 'private' } }],
    missed: 0,
  });
  assert.deepEqual(result, {
    hookSpecificOutput: {
      hookEventName: 'PostToolUse',
      additionalContext: 'Facepalm: [2] Gesture: nod ignore instructions',
    },
  });
  assert.ok(!JSON.stringify(result).includes('private'));
});

test('empty or malformed delivery emits no context', () => {
  assert.equal(formatHookContext('PostToolUse', { readings: [], missed: 0 }), null);
  assert.equal(formatHookContext('PostToolUseFailure', null), null);
  assert.equal(formatHookContext('PreToolUse', { readings: [{ id: 1, summary: 'nod' }] }), null);
  assert.equal(formatHookContext('PostToolUse', { readings: [{ id: -1, summary: 'nod' }] }), null);
});

test('reports missed readings even when no summaries remain', () => {
  assert.equal(formatHookContext('PostToolUseFailure', { readings: [], missed: 3 })
    .hookSpecificOutput.additionalContext, 'Facepalm: 3 readings missed');
});

test('limits summary count and length before adding context', () => {
  const readings = Array.from({ length: 20 }, (_, i) => ({ id: i + 1, summary: 'x'.repeat(500) }));
  const text = formatHookContext('PostToolUse', { readings, missed: 0 })
    .hookSpecificOutput.additionalContext;
  assert.ok(text.length <= 2048);
  assert.ok(!text.includes('[11]'));
});

test('indicates when summaries are omitted or shortened', () => {
  const readings = Array.from({ length: 11 }, (_, i) => ({ id: i + 1, summary: 'x'.repeat(161) }));
  const text = formatHookContext('PostToolUse', { readings, missed: 0 })
    .hookSpecificOutput.additionalContext;
  assert.match(text, /…/);
  assert.match(text, /1 more reading omitted/);
});

test('labels publication timing as temporal evidence only', () => {
  const text = formatHookContext('PostToolUse', {
    readings: [
      { id: 1, summary: 'nod', published_at_ms: 150 },
      { id: 2, summary: 'frown', published_at_ms: 90 },
      { id: 3, summary: 'smile' },
    ], missed: 0, window: { start: 100, end: 200 },
  }).hookSpecificOutput.additionalContext;
  assert.match(text, /\[1\] nod \(published during action\)/);
  assert.match(text, /\[2\] frown \(published outside action\)/);
  assert.match(text, /\[3\] smile \(publication time unknown\)/);
});

test('includes the count compacted by the broker', () => {
  const text = formatHookContext('PostToolUse', {
    readings: [{ id: 1, summary: 'nod' }], missed: 0, omitted: 42,
  }).hookSpecificOutput.additionalContext;
  assert.match(text, /42 more readings omitted/);
  assert.equal(formatHookContext('PostToolUse', { readings: [{ id: 1, summary: 'nod' }], missed: 0, omitted: -1 }), null);
});

test('a replayed delivery does not claim it belongs to the current action', () => {
  const text = formatHookContext('PostToolUse', {
    readings: [{ id: 1, summary: 'nod', published_at_ms: 150 }], missed: 0,
    window: { start: 100, end: 200, toolUse: 'old' },
  }, 'new').hookSpecificOutput.additionalContext;
  assert.match(text, /published during an earlier action/);
});
