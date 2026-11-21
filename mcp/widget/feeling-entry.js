// The widget's bootstrap, bundled by build.js into feeling-widget.html.
// Plumbing kept from Codex's draft (mcp/widget/feedback-entry.js): construct
// the App, mount the view, connect, and fall back to a status message if
// the host never finishes connecting. The view itself (feeling-view.js) is
// written fresh for the "pick the feeling" card, not salvaged from the draft.
import { App } from '@modelcontextprotocol/ext-apps/app-with-deps';
import { mountFeelingCard } from './feeling-view.js';

const app = new App({ name: 'Facepalm feeling card', version: '1.0.0' });
mountFeelingCard(app, document);
app.connect().catch(() => {
  document.querySelector('#status').textContent = 'Feeling card unavailable; tell Facepalm directly instead.';
});
