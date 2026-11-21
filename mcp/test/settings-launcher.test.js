import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { openFacepalmSettings } from '../settings-launcher.js';

const folder = { isDirectory: () => true, isFile: () => false, mode: 0o700 };
const file = { isDirectory: () => false, isFile: () => true, mode: 0o755 };
const broken = { isDirectory: () => false, isFile: () => false, mode: 0 };

test('macOS uses the fixed installed bundle and bounded open without a shell', async () => {
  const checked = [];
  let call;
  await openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }),
    lstat: async path => { checked.push(path); return path.endsWith('Facepalm.app') ? folder : file; },
    execFile: (path, args, options, done) => { if (path === '/usr/bin/plutil') return done(null, 'facepalm-menu\n'); call = { path, args, options }; done(null); },
  });
  assert.deepEqual(checked, ['/Users/test/Applications/Facepalm.app', '/Users/test/Applications/Facepalm.app/Contents/MacOS/facepalm-menu', '/Users/test/Applications/Facepalm.app/Contents/Info.plist']);
  assert.equal(call.path, '/usr/bin/open');
  assert.deepEqual(call.args, ['-a', '/Users/test/Applications/Facepalm.app', '--args', '--settings']);
  assert.equal(call.options.timeout, 3000);
  assert.equal(call.options.shell, false);
});

test('Linux requests the installed tray settings with fixed arguments', async () => {
  let call;
  let unref = false;
  await openFacepalmSettings({
    platform: 'linux', lstat: async path => { assert.equal(path, '/usr/bin/facepalm-menu'); return file; },
    spawn: (path, args, options) => { call = { path, args, options }; const child = new EventEmitter(); child.unref = () => { unref = true; }; queueMicrotask(() => child.emit('spawn')); return child; },
  });
  assert.deepEqual(call, { path: '/usr/bin/facepalm-menu', args: ['--settings'], options: { detached: true, stdio: 'ignore', shell: false } });
  assert.equal(unref, true);
});

test('with no platform given, the real process.platform picks the branch', async () => {
  // Works whichever of the two supported platforms the real host is: a
  // stat that fails every one of either platform's checks rejects either
  // way, without reaching execFile or spawn.
  await assert.rejects(openFacepalmSettings({
    userInfo: () => ({ homedir: '/nonexistent-test-home-xyz' }),
    lstat: async () => broken,
  }));
});

test('unavailable or unsafe installations and launcher errors fail without launch', async () => {
  let called = false;
  const fail = () => { called = true; };
  await assert.rejects(openFacepalmSettings({ platform: 'other', spawn: fail, execFile: fail }));
  await assert.rejects(openFacepalmSettings({ platform: 'linux', lstat: async () => ({ ...file, mode: 0o644 }), spawn: fail }));
  await assert.rejects(openFacepalmSettings({ platform: 'darwin', userInfo: () => ({ homedir: 'relative' }), lstat: async () => folder, execFile: fail }));
  assert.equal(called, false);
  // Each of the bundle's four checks fails on its own, independent of the
  // other three (macOS branch coverage).
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }), execFile: fail,
    lstat: async () => file,
  }));
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }), execFile: fail,
    lstat: async path => (path.endsWith('facepalm-menu') ? { ...file, mode: 0o644 } : folder),
  }));
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }), execFile: fail,
    lstat: async path => (path.endsWith('Info.plist') ? folder : (path.endsWith('Facepalm.app') ? folder : file)),
  }));
  assert.equal(called, false);
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }), lstat: async path => (path.endsWith('Facepalm.app') ? folder : file),
    execFile: (_path, _args, _options, done) => done(new Error('/private/secret')),
  }));
  await assert.rejects(openFacepalmSettings({
    platform: 'linux', lstat: async () => file,
    spawn: () => { const child = new EventEmitter(); queueMicrotask(() => child.emit('error', new Error('missing'))); return child; },
  }));
});

test('Linux launch timeout is bounded and stops the pending child', { timeout: 5000 }, async () => {
  let killed = false;
  const start = performance.now();
  await assert.rejects(openFacepalmSettings({
    platform: 'linux', lstat: async () => file,
    spawn: () => { const child = new EventEmitter(); child.kill = () => { killed = true; }; return child; },
  }));
  assert.equal(killed, true);
  assert.ok(performance.now() - start < 4500);
});

test('macOS launch timeout returns even if the launcher never calls back', { timeout: 5000 }, async () => {
  let killed = false;
  const start = performance.now();
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }),
    lstat: async path => (path.endsWith('Facepalm.app') ? folder : file),
    execFile: () => ({ kill: signal => { assert.equal(signal, 'SIGKILL'); killed = true; } }),
  }));
  assert.equal(killed, true);
  assert.ok(performance.now() - start < 4500);
});

test('a macOS timeout whose child has already gone does not throw from kill', { timeout: 5000 }, async () => {
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }),
    lstat: async path => (path.endsWith('Facepalm.app') ? folder : file),
    execFile: () => ({ kill: () => { throw new Error('ESRCH'); } }),
  }));
});

test('a Linux timeout whose child has already gone does not throw from kill', { timeout: 5000 }, async () => {
  await assert.rejects(openFacepalmSettings({
    platform: 'linux', lstat: async () => file,
    spawn: () => { const child = new EventEmitter(); child.kill = () => { throw new Error('ESRCH'); }; return child; },
  }));
});

test('a macOS launcher that throws synchronously is reported, not left pending', async () => {
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }),
    lstat: async path => (path.endsWith('Facepalm.app') ? folder : file),
    execFile: () => { throw new Error('out of handles'); },
  }), /out of handles/);
});

test('macOS rejects a bundle that names a different executable', async () => {
  const launched = [];
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }),
    lstat: async path => (path.endsWith('Facepalm.app') ? folder : file),
    execFile: (path, _args, _options, done) => { launched.push(path); done(null, 'unexpected'); },
  }));
  assert.deepEqual(launched, ['/usr/bin/plutil']);
});

// The three tests below each omit exactly one `ops` field, so the `??`
// falls back to the real `node:fs/promises`, `node:os` or
// `node:child_process` import, instead of always taking the mock every
// other test supplies. Each is still steered at a path this machine does
// not really have, by mocking everything else, so the real call can only
// fail harmlessly (ENOENT from a stat or a failed subprocess), never
// launch Facepalm or reach its camera. A developer's own machine can have
// a real `~/Applications/Facepalm.app` (this one does), which is why
// `userInfo` is mocked to a fake home in the `lstat` and `execFile` cases
// below rather than left real too.
test('the real lstat rejects a home with no installed bundle', async () => {
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/nonexistent-test-home-xyz' }),
  }));
});

test('the real userInfo is read, with lstat mocked to stop before any launch', async () => {
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', lstat: async () => file,
  }));
});

test('the real execFile fails harmlessly on a bundle that only lstat claims exists', { skip: process.platform !== 'darwin' }, async () => {
  await assert.rejects(openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/nonexistent-test-home-xyz' }),
    lstat: async path => (path.endsWith('Facepalm.app') ? folder : file),
  }));
});

test('the real spawn fails harmlessly on a tray that only lstat claims is there', async () => {
  // A hardcoded absolute path that does not exist fails with ENOENT the
  // same way on macOS as on Linux, so this needs no platform skip.
  await assert.rejects(openFacepalmSettings({ platform: 'linux', lstat: async () => file }));
});

test('real macOS plutil output is accepted without launching a window', { skip: process.platform !== 'darwin' }, async () => {
  const plist = fileURLToPath(new URL('../../scripts/facepalm-info.plist', import.meta.url));
  const executableName = execFileSync('/usr/bin/plutil', ['-extract', 'CFBundleExecutable', 'raw', '-o', '-', plist], { encoding: 'utf8' });
  let requested = false;
  await openFacepalmSettings({
    platform: 'darwin', userInfo: () => ({ homedir: '/Users/test' }),
    lstat: async path => (path.endsWith('Facepalm.app') ? folder : file),
    execFile: (path, _args, _options, done) => {
      if (path === '/usr/bin/plutil') return done(null, executableName);
      requested = true; done(null);
    },
  });
  assert.equal(requested, true);
});
