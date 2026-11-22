import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountFeelingCard } from '../feeling-view.js';

// A hand-rolled stand-in for the DOM surface feeling-view.js touches: no
// jsdom dependency for a handful of elements and one click event.
class FakeElement {
  constructor(tag) {
    this.tagName = tag;
    this.children = [];
    this.attributes = new Map();
    this.listeners = new Map();
    this.textContent = '';
    this.disabled = false;
  }
  setAttribute(name, value) { this.attributes.set(name, String(value)); }
  getAttribute(name) { return this.attributes.get(name); }
  addEventListener(type, handler) {
    const list = this.listeners.get(type) ?? [];
    list.push(handler);
    this.listeners.set(type, list);
  }
  click() { if (!this.disabled) for (const handler of this.listeners.get('click') ?? []) handler(); }
  replaceChildren(...nodes) { this.children = nodes; }
}

class FakeDocument {
  constructor() { this.registry = new Map(); }
  register(selector, element) { this.registry.set(selector, element); return element; }
  querySelector(selector) {
    if (!this.registry.has(selector)) throw new Error(`unknown selector ${selector}`);
    return this.registry.get(selector);
  }
  createElement(tag) { return new FakeElement(tag); }
}

function card() {
  const doc = new FakeDocument();
  const buttons = doc.register('#buttons', new FakeElement('div'));
  const status = doc.register('#status', new FakeElement('p'));
  return { doc, buttons, status };
}

const feelings = ['agreement', 'disagreement', 'engagement', 'annoyance', 'confused', 'unknown'];
const record = { id: 7, feeling: 'confused', feelings };

test('renders one button per feeling, in the service\'s order, with facepalm\'s read pre-highlighted', () => {
  const { doc, buttons, status } = card();
  const app = {};
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  assert.deepEqual(buttons.children.map((button) => button.textContent),
    ['Agreeing', 'Disagreeing', 'Interested', 'Annoyed', 'Confused', 'Not sure']);
  assert.deepEqual(buttons.children.map((button) => button.getAttribute('aria-pressed')),
    ['false', 'false', 'false', 'false', 'true', 'false']);
  assert.equal(status.textContent, '');
});

test('shows an unmapped feeling name as itself', () => {
  const { doc, buttons } = card();
  const app = {};
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: { id: 1, feeling: 'novel', feelings: ['novel'] } });
  assert.equal(buttons.children[0].textContent, 'novel');
});

test('a click sends tell_feeling for that interpretation, disables every button, and thanks the person', async () => {
  const { doc, buttons, status } = card();
  const calls = [];
  const app = { callServerTool: async (call) => { calls.push(call); return { content: [] }; } };
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  buttons.children[1].click();
  await Promise.resolve().then(() => {});
  await Promise.resolve().then(() => {});
  assert.deepEqual(calls, [{ name: 'tell_feeling', arguments: { id: 7, feeling: 'disagreement' } }]);
  assert.ok(buttons.children.every((button) => button.disabled));
  assert.equal(status.textContent, 'Thanks — Facepalm will learn from this.');
});

test('tells the agent the pick through updateModelContext, when the host supports it (#133)', async () => {
  const { doc, buttons, status } = card();
  const updates = [];
  const structuredContent = { id: 7, read: 'confused', picked: 'disagreement', feeling: 'disagreement' };
  const app = {
    callServerTool: async () => ({ content: [], structuredContent }),
    getHostCapabilities: () => ({ updateModelContext: { text: {} } }),
    updateModelContext: async (params) => { updates.push(params); },
  };
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  buttons.children[1].click();
  await Promise.resolve().then(() => {}).then(() => {}).then(() => {});
  assert.deepEqual(updates, [{ structuredContent }]);
  assert.equal(status.textContent, 'Thanks — Facepalm will learn from this.');
});

test('never calls updateModelContext when the host does not support it', async () => {
  const { doc, buttons, status } = card();
  const app = {
    callServerTool: async () => ({ content: [], structuredContent: { id: 7, read: 'confused', picked: 'disagreement', feeling: 'disagreement' } }),
    getHostCapabilities: () => ({}),
    updateModelContext: async () => assert.fail('must not be called'),
  };
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  buttons.children[1].click();
  await Promise.resolve().then(() => {}).then(() => {});
  assert.equal(status.textContent, 'Thanks — Facepalm will learn from this.');
});

test('never calls updateModelContext when the host declares no capabilities at all', async () => {
  const { doc, buttons, status } = card();
  const app = { callServerTool: async () => ({ content: [], structuredContent: { id: 7, read: 'confused', picked: 'disagreement', feeling: 'disagreement' } }) };
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  buttons.children[1].click();
  await Promise.resolve().then(() => {}).then(() => {});
  assert.equal(status.textContent, 'Thanks — Facepalm will learn from this.');
});

test('swallows a rejected updateModelContext, keeping the thank-you shown', async () => {
  const { doc, buttons, status } = card();
  const app = {
    callServerTool: async () => ({ content: [], structuredContent: { id: 7, read: 'confused', picked: 'disagreement', feeling: 'disagreement' } }),
    getHostCapabilities: () => ({ updateModelContext: { text: {} } }),
    updateModelContext: async () => { throw new Error('host refused'); },
  };
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  buttons.children[1].click();
  await Promise.resolve().then(() => {}).then(() => {}).then(() => {}).then(() => {});
  assert.equal(status.textContent, 'Thanks — Facepalm will learn from this.');
});

test('a second click is refused: no second call, no change once sent', async () => {
  const { doc, buttons } = card();
  const calls = [];
  const app = { callServerTool: async (call) => { calls.push(call); return { content: [] }; } };
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  buttons.children[1].click();
  buttons.children[1].click();
  buttons.children[2].click();
  await Promise.resolve().then(() => {});
  assert.equal(calls.length, 1);
});

test('a refused or failed feeling says so, and leaves the buttons disabled', async () => {
  for (const callServerTool of [async () => ({ isError: true }), async () => { throw new Error('closed'); }, async () => undefined]) {
    const { doc, buttons, status } = card();
    const app = { callServerTool };
    mountFeelingCard(app, doc);
    app.ontoolresult({ structuredContent: record });
    buttons.children[0].click();
    await Promise.resolve().then(() => {}).then(() => {}).then(() => {});
    assert.equal(status.textContent, 'Facepalm could not take that feeling.');
    assert.ok(buttons.children.every((button) => button.disabled));
  }
});

test('an unavailable or malformed reading says so and renders no buttons', () => {
  const shapes = [
    { isError: true, structuredContent: record },
    undefined,
    { structuredContent: undefined },
    { structuredContent: { id: '7', feeling: 'confused', feelings } },
    { structuredContent: { id: 7, feeling: 'confused', feelings: 'confused' } },
    { structuredContent: { id: 7, feeling: 'missing', feelings } },
  ];
  for (const result of shapes) {
    const { doc, buttons, status } = card();
    const app = {};
    mountFeelingCard(app, doc);
    app.ontoolresult(result);
    assert.equal(buttons.children.length, 0);
    assert.equal(status.textContent, 'This reading is unavailable.');
  }
});

test('ignores every tool result after the first: one card is one interpretation', () => {
  const { doc, buttons } = card();
  const app = {};
  mountFeelingCard(app, doc);
  app.ontoolresult({ structuredContent: record });
  app.ontoolresult({ structuredContent: { id: 9, feeling: 'agreement', feelings } });
  assert.equal(buttons.children.length, 6);
  assert.equal(buttons.children[0].getAttribute('aria-pressed'), 'false');
  assert.equal(buttons.children[4].getAttribute('aria-pressed'), 'true');
});
