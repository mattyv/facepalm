// The "pick the feeling" card (see the issue's design notes on #61 item 5):
// it asks what the person felt, never whether a gesture happened, with every
// feeling as a button, Facepalm's read pre-highlighted, and one click either
// way. No interpretation ID, basis, eligibility or retention text is shown.
const labels = {
  agreement: 'Agreeing',
  disagreement: 'Disagreeing',
  engagement: 'Interested',
  annoyance: 'Annoyed',
  confused: 'Confused',
  unknown: 'Not sure',
};

function validRecord(record) {
  return record !== undefined && typeof record.id === 'number' &&
    Array.isArray(record.feelings) && record.feelings.includes(record.feeling);
}

export function mountFeelingCard(app, doc) {
  const buttons = doc.querySelector('#buttons');
  const status = doc.querySelector('#status');
  let initialized = false;

  // Disabling every button, synchronously and before anything async, is the
  // whole guard: a disabled button fires no click, so a second click is
  // refused by the browser itself.
  function send(id, feeling, all) {
    all.forEach((button) => { button.disabled = true; });
    Promise.resolve(app.callServerTool({ name: 'tell_feeling', arguments: { id, feeling } }))
      .then((result) => {
        status.textContent = result && !result.isError
          ? 'Thanks — Facepalm will learn from this.'
          : 'Facepalm could not take that feeling.';
      })
      .catch(() => { status.textContent = 'Facepalm could not take that feeling.'; });
  }

  app.ontoolresult = (result) => {
    if (initialized) return;
    initialized = true;
    const record = result?.structuredContent;
    if (result?.isError || !validRecord(record)) {
      status.textContent = 'This reading is unavailable.';
      return;
    }
    const nodes = record.feelings.map((feeling) => {
      const button = doc.createElement('button');
      button.type = 'button';
      button.textContent = labels[feeling] ?? feeling;
      button.setAttribute('aria-pressed', feeling === record.feeling ? 'true' : 'false');
      return button;
    });
    buttons.replaceChildren(...nodes);
    nodes.forEach((button, index) => {
      button.addEventListener('click', () => send(record.id, record.feelings[index], nodes));
    });
  };
}
