// A helper process for hook-broker.test.js's "cannot be secured" case. It
// runs the real, unmodified hook-broker.js in its own process, so its
// coverage still attributes to mcp/hook-broker.js with no query-string
// variant to merge: mocking node:fs/promises in-process would be too late,
// since hook-broker.test.js's own top-level import already links the real
// module before any test body runs. Exits 0 only if startHookBroker
// rejected with exactly the denied chmod's message.
import { mock } from 'node:test';

const real = await import('node:fs/promises');
mock.module('node:fs/promises', {
  namedExports: { ...real, chmod: async () => { throw new Error('denied'); } },
});
const { startHookBroker } = await import('../hook-broker.js');

try {
  await startHookBroker({ path: process.argv[2], session: {} });
  process.exit(1);
} catch (error) {
  process.exit(error.message === 'denied' ? 0 : 2);
}
