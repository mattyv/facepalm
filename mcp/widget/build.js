// Bundles feeling-entry.js into feeling-widget.html, checked into git next
// to this script. Plumbing kept from Codex's draft (mcp/widget/build.js),
// with one fix: String.prototype.replace treats "$&", "$1" and "$$" in a
// STRING replacement as its own substitution patterns, even though the
// search argument here is a plain string, not a regular expression. A
// bundled script that happens to contain one of those sequences (such as a
// literal "$&") would be silently corrupted. Passing a function instead
// inserts its return value with no pattern interpretation at all.
import { build } from 'esbuild';
import { readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const { outputFiles } = await build({
  entryPoints: [fileURLToPath(new URL('feeling-entry.js', import.meta.url))],
  bundle: true,
  format: 'esm',
  write: false,
  minify: true,
});
const script = outputFiles[0].text.replaceAll('</script', '<\\/script');
const template = await readFile(new URL('feeling-template.html', import.meta.url), 'utf8');
const marker = '/* BUNDLED_SCRIPT */';
const html = template.replace(marker, () => script);
const target = new URL('../feeling-widget.html', import.meta.url);
if (process.argv.includes('--check')) {
  const committed = await readFile(target, 'utf8');
  if (committed !== html) throw new Error('Widget bundle differs from source; run npm run build:widget.');
} else {
  await writeFile(target, html);
}
