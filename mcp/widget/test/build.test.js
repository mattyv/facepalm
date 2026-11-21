import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const run = promisify(execFile);
const buildSource = fileURLToPath(new URL('../build.js', import.meta.url));

// A fixture widget/ directory under mcp/, so the copied build.js still
// resolves esbuild from mcp/node_modules by walking up from its own path.
async function fixture(entry, template) {
  const root = await mkdtemp(path.join(fileURLToPath(new URL('.', import.meta.url)), '.fixture-'));
  const widget = path.join(root, 'widget');
  await mkdir(widget);
  await writeFile(path.join(widget, 'build.js'), await readFile(buildSource, 'utf8'));
  await writeFile(path.join(widget, 'feeling-entry.js'), entry);
  await writeFile(path.join(widget, 'feeling-template.html'), template);
  return { root, widget, target: path.join(root, 'feeling-widget.html') };
}

test('bundles the entry point into the template, minified, with no network and no leftover import or export', async () => {
  const { widget, target } = await fixture(
    "document.title = 'probe';\n",
    '<!doctype html><body><script type="module">/* BUNDLED_SCRIPT */</script></body>',
  );
  try {
    await run(process.execPath, [path.join(widget, 'build.js')]);
    const html = await readFile(target, 'utf8');
    assert.match(html, /document\.title="probe"/);
    const script = html.match(/<script type="module">([\s\S]*)<\/script>/)[1];
    assert.doesNotThrow(() => new vm.Script(script));
    assert.doesNotMatch(script, /\bimport[ ({]/);
    assert.doesNotMatch(script, /\bexport[ {]/);
  } finally { await rm(path.dirname(widget), { recursive: true, force: true }); }
});

test('never corrupts a bundled "$&" with String.replace\'s own substitution pattern', async () => {
  const { widget, target } = await fixture(
    "document.title = '$&MARKER-PROBE';\n",
    '<!doctype html><body><script type="module">/* BUNDLED_SCRIPT */</script></body>',
  );
  try {
    await run(process.execPath, [path.join(widget, 'build.js')]);
    const html = await readFile(target, 'utf8');
    assert.match(html, /\$&MARKER-PROBE/);
    assert.doesNotMatch(html, /BUNDLED_SCRIPT.*MARKER-PROBE/);
  } finally { await rm(path.dirname(widget), { recursive: true, force: true }); }
});

test('--check passes when the committed bundle matches the source, and fails when it does not', async () => {
  const { widget, target } = await fixture(
    "document.title = 'a';\n",
    '<!doctype html><body><script type="module">/* BUNDLED_SCRIPT */</script></body>',
  );
  try {
    await run(process.execPath, [path.join(widget, 'build.js')]);
    await run(process.execPath, [path.join(widget, 'build.js'), '--check']);
    await writeFile(target, `${await readFile(target, 'utf8')}<!-- stale -->`);
    await assert.rejects(run(process.execPath, [path.join(widget, 'build.js'), '--check']),
      /differs/);
  } finally { await rm(path.dirname(widget), { recursive: true, force: true }); }
});

test('the real widget bundle checked into git regenerates byte for byte and then passes --check', async () => {
  const real = fileURLToPath(new URL('../build.js', import.meta.url));
  const target = fileURLToPath(new URL('../../feeling-widget.html', import.meta.url));
  const before = await readFile(target, 'utf8');
  await run(process.execPath, [real]);
  assert.equal(await readFile(target, 'utf8'), before);
  await run(process.execPath, [real, '--check']);
  try {
    await writeFile(target, `${before}<!-- stale -->`);
    await assert.rejects(run(process.execPath, [real, '--check']), /differs/);
  } finally { await writeFile(target, before); }
});
