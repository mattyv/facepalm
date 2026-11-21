import { execFile, spawn } from 'node:child_process';
import { lstat } from 'node:fs/promises';
import { userInfo } from 'node:os';
import { isAbsolute, join } from 'node:path';

// How long a launch attempt may take before it is abandoned (#36, #61 item 8).
const timeoutMs = 3000;

// Requests the installed Facepalm Settings window, on the fixed path each
// platform's build installs it at (never a path the caller supplies, and
// never through a shell): docs/local-app.md and the Debian package
// (CMakeLists.txt). This only asks the menu app to show its window; it does
// not touch the camera, grant consent, or verify the window became visible.
export async function openFacepalmSettings(ops = {}) {
  const platform = ops.platform ?? process.platform;
  const inspect = ops.lstat ?? lstat;
  if (platform === 'darwin') {
    const home = (ops.userInfo ?? userInfo)().homedir;
    if (typeof home !== 'string' || !isAbsolute(home)) throw new Error('unavailable');
    const app = join(home, 'Applications', 'Facepalm.app');
    const bundle = await inspect(app);
    const executable = await inspect(join(app, 'Contents', 'MacOS', 'facepalm-menu'));
    const plistPath = join(app, 'Contents', 'Info.plist');
    const plist = await inspect(plistPath);
    if (!bundle.isDirectory() || !executable.isFile() || !(executable.mode & 0o111) || !plist.isFile()) {
      throw new Error('unavailable');
    }
    const run = (path, args) => new Promise((resolve, reject) => {
      let child;
      const timer = setTimeout(() => {
        try { child?.kill('SIGKILL'); } catch { /* already gone */ }
        reject(new Error('timeout'));
      }, timeoutMs);
      try {
        child = (ops.execFile ?? execFile)(path, args,
          { timeout: timeoutMs, maxBuffer: 1024, shell: false }, (error, stdout) => {
            clearTimeout(timer);
            if (error) reject(error); else resolve(stdout);
          });
      } catch (error) { clearTimeout(timer); reject(error); }
    });
    // Checked, not merely assumed, before a possibly attacker-placed bundle
    // of that name is asked to open: see docs/decisions.md for why this is
    // a check, not a proof the binary itself is trustworthy.
    const executableName = await run('/usr/bin/plutil', ['-extract', 'CFBundleExecutable', 'raw', '-o', '-', plistPath]);
    if (executableName !== 'facepalm-menu' && executableName !== 'facepalm-menu\n') throw new Error('unavailable');
    await run('/usr/bin/open', ['-a', app, '--args', '--settings']);
    return;
  }
  if (platform === 'linux') {
    const path = '/usr/bin/facepalm-menu';
    const executable = await inspect(path);
    if (!executable.isFile() || !(executable.mode & 0o111)) throw new Error('unavailable');
    await new Promise((resolve, reject) => {
      const child = (ops.spawn ?? spawn)(path, ['--settings'], { detached: true, stdio: 'ignore', shell: false });
      const timer = setTimeout(() => { try { child.kill('SIGKILL'); } catch { /* already gone */ } reject(new Error('timeout')); }, timeoutMs);
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.once('spawn', () => { clearTimeout(timer); child.unref(); resolve(); });
    });
    return;
  }
  throw new Error('unavailable');
}
