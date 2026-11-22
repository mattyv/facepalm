import { test } from 'node:test';
import assert from 'node:assert/strict';
import { feelingResult } from '../feeling-result.js';

test('a pick replaces the read: feeling is the pick, both values carried', () => {
  const result = feelingResult({ id: 7, read: 'agreement', picked: 'disagreement', text: 'Feeling taken.' });
  assert.deepEqual(result, {
    content: [{ type: 'text', text: 'Feeling taken.' }],
    structuredContent: { id: 7, read: 'agreement', picked: 'disagreement', feeling: 'disagreement' },
  });
});

test('a pick that confirms the read still reports it as the pick', () => {
  const result = feelingResult({ id: 7, read: 'agreement', picked: 'agreement', text: 'Feeling taken.' });
  assert.deepEqual(result.structuredContent, { id: 7, read: 'agreement', picked: 'agreement', feeling: 'agreement' });
});

test('with no pick, the read stands as the feeling', () => {
  const result = feelingResult({ id: 7, read: 'agreement', text: 'The person did not pick a feeling.' });
  assert.deepEqual(result.structuredContent, { id: 7, read: 'agreement', picked: undefined, feeling: 'agreement' });
  assert.equal('isError' in result, false);
});

test('a pick with no read at all still carries it as the feeling', () => {
  const result = feelingResult({ id: 7, picked: 'disagreement', text: 'Feeling taken.' });
  assert.deepEqual(result.structuredContent, { id: 7, read: undefined, picked: 'disagreement', feeling: 'disagreement' });
});

test('isError is set only when asked, and content is always the given text', () => {
  const ok = feelingResult({ id: 7, read: 'agreement', text: 'Feeling taken.' });
  assert.equal('isError' in ok, false);
  const failed = feelingResult({ id: 7, read: 'agreement', picked: 'disagreement', isError: true, text: 'refused' });
  assert.equal(failed.isError, true);
  assert.deepEqual(failed.content, [{ type: 'text', text: 'refused' }]);
});

test('no structuredContent at all when neither a read nor a pick is known', () => {
  const result = feelingResult({ id: 7, text: 'Facepalm no longer recognizes that interpretation.', isError: true });
  assert.deepEqual(result, {
    isError: true,
    content: [{ type: 'text', text: 'Facepalm no longer recognizes that interpretation.' }],
  });
});
