import { test, mock } from 'node:test';
import assert from 'node:assert/strict';

// feeling-entry.js is the widget's bootstrap: it must construct the App,
// mount the view onto it and the real document, and connect. Both
// collaborators are mocked; this file is never loaded in a browser context
// by the test itself, only under Node with its two imports replaced.
let appOptions;
let mountedWith;
let connectResult;

class FakeApp {
  constructor(options) { appOptions = options; }
  connect() { return connectResult(); }
}

mock.module('@modelcontextprotocol/ext-apps/app-with-deps', { namedExports: { App: FakeApp } });
mock.module('../feeling-view.js', {
  namedExports: { mountFeelingCard: (app, doc) => { mountedWith = { app, doc }; } },
});

function fakeDocument() {
  const status = { textContent: '' };
  return { querySelector: (selector) => (selector === '#status' ? status : undefined), status };
}

test('constructs the app, mounts the view, and connects', async () => {
  connectResult = () => Promise.resolve();
  const previousDocument = globalThis.document;
  globalThis.document = fakeDocument();
  try {
    await import(`../feeling-entry.js?${Math.random()}`);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(appOptions.name, 'Facepalm feeling card');
    assert.ok(mountedWith.app instanceof FakeApp);
    assert.equal(mountedWith.doc, globalThis.document);
  } finally { globalThis.document = previousDocument; }
});

test('a failed connection falls back to a status message, not a crash', async () => {
  connectResult = () => Promise.reject(new Error('no host'));
  const previousDocument = globalThis.document;
  globalThis.document = fakeDocument();
  try {
    await import(`../feeling-entry.js?${Math.random()}`);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(globalThis.document.status.textContent, 'Feeling card unavailable; tell Facepalm directly instead.');
  } finally { globalThis.document = previousDocument; }
});
