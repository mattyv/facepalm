import { randomUUID } from 'node:crypto';
import net from 'node:net';

const maxReplyBytes = 1024 * 1024;

// One session owns one socket. Losing or cancelling it is terminal: reconnecting
// would create a new feed cursor and could misattribute a consumed response.
// Each session names itself, so several sessions showing one approved token
// are separate clients of the service (#37).
export class SocketSession {
  constructor(path, token) {
    this.path = path;
    this.name = `mcp-${randomUUID()}`;
    this.token = token;
    this.tail = Promise.resolve();
    this.queued = 0;
    this.socket = undefined;
    this.pending = undefined;
    this.buffer = '';
    this.closed = false;
  }

  close() {
    this.closed = true;
    this.socket?.destroy();
    this.pending?.reject(new Error('closed'));
    this.pending = undefined;
  }

  async connect(signal) {
    if (this.closed) throw new Error('closed');
    if (this.socket) return;
    const socket = net.createConnection(this.path);
    this.socket = socket;
    socket.setEncoding('utf8');
    socket.on('data', (chunk) => {
      this.buffer += chunk;
      if (Buffer.byteLength(this.buffer) > maxReplyBytes) return this.close();
      const newline = this.buffer.indexOf('\n');
      if (newline < 0) return;
      const line = this.buffer.slice(0, newline);
      this.buffer = this.buffer.slice(newline + 1);
      if (!this.pending || this.buffer.length) return this.close();
      const pending = this.pending;
      this.pending = undefined;
      pending.resolve(line);
    });
    socket.on('error', () => this.close());
    socket.on('close', () => this.close());
    const hello = await this.exchange(this.token ? `hello ${this.name} ${this.token}` : `hello ${this.name}`, signal, 5000);
    if (hello?.ok !== true) { this.close(); throw new Error('hello failed'); }
  }

  async exchange(command, signal, timeout) {
    if (signal?.aborted || this.closed) throw new Error('closed');
    return new Promise((resolve, reject) => {
      const finish = (error, line) => {
        clearTimeout(timer);
        signal?.removeEventListener('abort', abort);
        if (error) return reject(error);
        try { resolve(JSON.parse(line)); } catch { this.close(); reject(new Error('invalid reply')); }
      };
      const abort = () => { this.close(); finish(new Error('cancelled')); };
      const timer = setTimeout(() => { this.close(); finish(new Error('timeout')); }, timeout);
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
      await this.connect(signal);
      return this.exchange(command, signal, timeout);
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
