import { chmod, lstat } from 'node:fs/promises';
import net from 'node:net';
import path from 'node:path';

const identifier = /^[A-Za-z0-9_-]{1,128}$/;
const events = new Set(['PreToolUse', 'PostToolUse', 'PostToolUseFailure']);
const toolNames = new Set(['Bash', 'Read', 'Write', 'Edit', 'Glob', 'Grep', 'Other']);
const maxBytes = 65536;

function validEvent(value) {
  return value && events.has(value.event) && toolNames.has(value.tool) &&
    typeof value.session === 'string' && identifier.test(value.session) &&
    typeof value.toolUse === 'string' && identifier.test(value.toolUse);
}

function safeDelivery(value, window) {
  if (!Array.isArray(value?.readings) || !Number.isSafeInteger(value.missed) || value.missed < 0 ||
      value.readings.length > 1000) throw new Error('invalid delivery');
  const readings = value.readings.slice(0, 10).map((reading) => {
    if (!Number.isSafeInteger(reading?.id) || reading.id <= 0 ||
        typeof reading.summary !== 'string' || reading.summary.length > 4096 ||
        (reading.published_at_ms !== undefined && !Number.isSafeInteger(reading.published_at_ms))) {
      throw new Error('invalid reading');
    }
    const summary = reading.summary.length > 512 ? `${reading.summary.slice(0, 511)}…` : reading.summary;
    return { id: reading.id, summary, published_at_ms: reading.published_at_ms };
  });
  return { readings, omitted: value.readings.length - readings.length, missed: value.missed, window };
}

// The adapter remains the sole native session owner. A delivery remains leased
// until its hook acknowledges writing context; an interrupted hook can retry it.
export async function startHookBroker({ path: socketPath, session }) {
  const dir = await lstat(path.dirname(socketPath));
  if (!dir.isDirectory() || dir.uid !== process.getuid() || (dir.mode & 0o077) !== 0) {
    throw new Error('hook directory must be private and owned by this user');
  }
  try { await lstat(socketPath); throw new Error('hook socket path already exists'); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  let closed = false;
  let hostSession;
  let buffered;
  let lease;
  let tail = Promise.resolve();
  const actions = new Map();
  const sockets = new Set();
  const server = net.createServer((socket) => {
    sockets.add(socket);
    socket.setEncoding('utf8');
    socket.setTimeout(4000, () => socket.destroy());
    socket.on('error', () => {});
    socket.on('close', () => { sockets.delete(socket); if (lease === socket) lease = undefined; });
    let buffer = '';
    let seen = false;
    socket.on('data', (chunk) => {
      buffer += chunk;
      if (Buffer.byteLength(buffer) > maxBytes) return socket.destroy();
      const newline = buffer.indexOf('\n');
      if (newline < 0) return;
      const line = buffer.slice(0, newline);
      buffer = buffer.slice(newline + 1);
      if (buffer.length) return socket.destroy();
      if (seen) {
        if (line === 'ack' && lease === socket) { buffered = undefined; lease = undefined; }
        return socket.end();
      }
      seen = true;
      let value;
      try { value = JSON.parse(line); } catch { return socket.end('{"ok":false}\n'); }
      const work = async () => {
        if (closed || socket.destroyed) return;
        if (!validEvent(value) || (hostSession && hostSession !== value.session)) {
          return socket.end('{"ok":false}\n');
        }
        hostSession ??= value.session;
        const key = `${value.session}:${value.toolUse}`;
        if (value.event === 'PreToolUse') {
          if (actions.size >= 128) actions.delete(actions.keys().next().value);
          actions.set(key, Date.now());
          return socket.end('{"ok":true}\n');
        }
        const start = actions.get(key) ?? null;
        actions.delete(key);
        if (lease || (!buffered && session.queued !== 0)) return socket.end('{"ok":false,"busy":true}\n');
        if (!buffered) {
          try {
            const delivery = await session.request('wait 0', undefined, 5000);
            buffered = safeDelivery(delivery, { start, end: Date.now(), toolUse: value.toolUse });
          } catch { return socket.end('{"ok":false}\n'); }
        }
        if (closed || socket.destroyed) return;
        lease = socket;
        socket.write(`${JSON.stringify({ ok: true, delivery: buffered })}\n`);
      };
      tail = tail.then(work, work);
    });
  });
  server.maxConnections = 32;
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(socketPath, () => { server.off('error', reject); resolve(); });
  });
  try { await chmod(socketPath, 0o600); }
  catch (error) { await new Promise((resolve) => server.close(resolve)); throw error; }
  return {
    async close() {
      closed = true;
      for (const socket of sockets) socket.destroy();
      await new Promise((resolve) => server.close(resolve));
      await tail;
    },
  };
}
