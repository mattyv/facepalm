import { randomUUID } from 'node:crypto';
import net from 'node:net';

const maxReplyBytes = 1024 * 1024;
const minBackoffMs = 1000;
const maxBackoffMs = 30000;

// What session.request() rejects with when it could not be exchanged
// because this session is not connected right now, rather than because
// the caller cancelled it or closed the session for good (#117): the one
// thing a tool needs to tell "Facepalm is off" apart from every other
// failure, without matching error text across files.
export class OfflineError extends Error {}

// One session owns one socket (specs/McpSocket.tla). Losing the link is
// not the same as closing the session (#117): a lost link is retried
// later, with a backoff bounded by maxBackoffMs, reset once a connection
// succeeds. Closing the session, or cancelling a request already in
// flight, is still terminal: a new feed cursor on a fresh name would be
// safe to reconnect to, but a cancelled exchange could still be mid write
// when the next one starts, so cancellation never retries either. Each
// session names itself, so several sessions showing one approved token
// are separate clients of the service (#37).
export class SocketSession {
  // `token` is this session's approved token: a string, used as is (the
  // tests'), or a zero-argument function read fresh for every connection
  // attempt (main.js's, so a token file approved after this process
  // started is picked up without a restart, #117); undefined, either way,
  // sends hello with no token, as a daemon run without approval expects.
  // `now`, milliseconds since the epoch, is the session's own clock,
  // replaceable so a test can move its backoff without really waiting.
  constructor(path, token, now = Date.now) {
    this.path = path;
    this.name = `mcp-${randomUUID()}`;
    this.token = token;
    this.now = now;
    this.tail = Promise.resolve();
    this.queued = 0;
    this.socket = undefined;
    this.pending = undefined;
    this.buffer = '';
    this.closed = false;
    this.backoffMs = minBackoffMs;
    this.retryAt = 0;
  }

  // Closes this session for good (specs/McpSocket.tla's Cancel): no later
  // request ever reconnects.
  close() {
    this.closed = true;
    this.disconnect();
  }

  // Tears down the current socket, if any, without ending the session
  // (specs/McpSocket.tla's LinkDown, not its Cancel): a later request may
  // reconnect, once this session's own backoff has run out. Bounded at
  // maxBackoffMs, and only once per link lost, in case 'error' and
  // 'close' both fire for the one failure: a socket already cleared
  // means this link's own backoff was already bumped.
  disconnect() {
    const wasConnected = this.socket !== undefined;
    this.socket?.destroy();
    this.socket = undefined;
    this.buffer = '';
    this.pending?.reject(new Error('disconnected'));
    this.pending = undefined;
    if (wasConnected) {
      this.retryAt = this.now() + this.backoffMs;
      this.backoffMs = Math.min(this.backoffMs * 2, maxBackoffMs);
    }
  }

  currentToken() {
    if (typeof this.token !== 'function') return this.token;
    try { return this.token(); } catch { return undefined; }
  }

  // specs/McpSocket.tla's ReconnectSucceeds or ReconnectFails: dials once
  // backoff allows it, and otherwise leaves link down and backoff
  // untouched for connect() to report. Only connect() calls this.
  async dial(signal) {
    const socket = net.createConnection(this.path);
    this.socket = socket;
    socket.setEncoding('utf8');
    socket.on('data', (chunk) => {
      this.buffer += chunk;
      if (Buffer.byteLength(this.buffer) > maxReplyBytes) return this.disconnect();
      const newline = this.buffer.indexOf('\n');
      if (newline < 0) return;
      const line = this.buffer.slice(0, newline);
      this.buffer = this.buffer.slice(newline + 1);
      if (!this.pending || this.buffer.length) return this.disconnect();
      const pending = this.pending;
      this.pending = undefined;
      pending.resolve(line);
    });
    socket.on('error', () => this.disconnect());
    socket.on('close', () => this.disconnect());
    const token = this.currentToken();
    const hello = await this.exchange(token ? `hello ${this.name} ${token}` : `hello ${this.name}`, signal, 5000);
    if (hello?.ok !== true) { this.disconnect(); throw new Error('hello failed'); }
  }

  // Connects if not already connected: specs/McpSocket.tla's state="open"
  // guards every reconnect attempt (Cancel is terminal), and backoff = 0
  // guards each one's timing. A dial that fails has already bounded the
  // next backoff through disconnect(); one that succeeds resets it to
  // its minimum, for a run of failures that follows a healthy stretch.
  async connect(signal) {
    if (this.closed) throw new Error('closed');
    if (this.socket) return;
    if (this.now() < this.retryAt) throw new Error('offline');
    await this.dial(signal);
    this.backoffMs = minBackoffMs;
  }

  async exchange(command, signal, timeout) {
    if (signal?.aborted || this.closed) throw new Error('closed');
    return new Promise((resolve, reject) => {
      const finish = (error, line) => {
        clearTimeout(timer);
        signal?.removeEventListener('abort', abort);
        if (error) return reject(error);
        try { resolve(JSON.parse(line)); } catch { this.disconnect(); reject(new Error('invalid reply')); }
      };
      // Cancelling is terminal (specs/McpSocket.tla's Cancel), unlike
      // every other way this exchange can end.
      const abort = () => { this.close(); finish(new Error('cancelled')); };
      const timer = setTimeout(() => { this.disconnect(); finish(new Error('timeout')); }, timeout);
      this.pending = { resolve: (line) => finish(null, line), reject: (error) => finish(error) };
      signal?.addEventListener('abort', abort, { once: true });
      this.socket.write(`${command}\n`);
    });
  }

  request(command, signal, timeout = 5000) {
    if (this.queued >= 16) return Promise.reject(new Error('busy'));
    this.queued++;
    const task = this.tail.then(async () => {
      if (signal?.aborted) throw new Error('cancelled');
      try {
        await this.connect(signal);
        return await this.exchange(command, signal, timeout);
      } catch (error) {
        // 'cancelled' and 'closed' are this session's own terminal
        // outcomes; every other reason this did not reach the wire is
        // reported as OfflineError, whether it is this attempt's own
        // dial failing or an earlier one still backing off (#117).
        if (error.message === 'cancelled' || error.message === 'closed') throw error;
        throw new OfflineError('offline');
      }
    });
    this.tail = task.finally(() => { this.queued--; }).catch(() => {});
    if (!signal) return task;
    if (signal.aborted) return Promise.reject(new Error('cancelled'));
    return new Promise((resolve, reject) => {
      const abort = () => reject(new Error('cancelled'));
      signal.addEventListener('abort', abort, { once: true });
      task.then(resolve, reject).finally(() => signal.removeEventListener('abort', abort));
    });
  }
}
