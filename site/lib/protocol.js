export function cleanLine(line) {
  return line.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '').replace(/\r/g, '');
}

export class LineFramer {
  constructor(onLine, limit = 16384) { this.onLine = onLine; this.limit = limit; this.pending = ''; this.decoder = new TextDecoder(); }
  push(bytes) {
    this.pending += this.decoder.decode(bytes, { stream: true });
    if (this.pending.length > this.limit) throw new Error('Serial frame too large');
    let end;
    while ((end = this.pending.indexOf('\n')) !== -1) {
      const line = cleanLine(this.pending.slice(0, end)).trim();
      this.pending = this.pending.slice(end + 1);
      if (line) this.onLine(line);
    }
  }
}

export class SerialConsole {
  constructor({ log = () => {}, readyDelay = 3000 } = {}) {
    this.log = log; this.readyDelay = readyDelay; this.port = null; this.tail = Promise.resolve(); this.active = null; this.quiet = false;
    this.framer = new LineFramer(line => this.line(line));
  }
  get connected() { return Boolean(this.port?.writable && this.reader); }
  line(line) {
    const request = this.active;
    // Radio messages can include an SSID, and command echoes can include secrets.
    // Suppress *all* received output while a sensitive transaction is in flight.
    if (!this.quiet && !/wifi|password|ssid|SAVE HEX|save restore/i.test(line)) this.log(line);
    if (!request) return;
    if (/^ERR(?:\s|$)/.test(line)) { request.fail(new Error(request.sensitive ? 'Device rejected WiFi configuration (details withheld)' : line)); return; }
    if (request.dataPrefix && line.startsWith(request.dataPrefix)) request.data = line.slice(request.dataPrefix.length);
    if (request.accept(line)) {
      if (request.dataPrefix && request.data === undefined) request.fail(new Error('Acknowledgement received without framed response data'));
      else request.done({ line, data: request.data });
    }
  }
  async connect(port) {
    if (this.port) throw new Error('Disconnect the current console first');
    this.port = port;
    try {
      await port.open({ baudRate: 115200 });
      this.reader = port.readable.getReader();
      this.writer = port.writable.getWriter();
      this.readTask = this.readLoop();
      await new Promise(resolve => setTimeout(resolve, this.readyDelay));
      await this.command('help', { accept: line => /^OK help(?:\s|$)/.test(line) || /^reboot\s+restart$/.test(line), timeout: 8000 });
      this.log('Console ready; help frame acknowledged.');
    } catch (error) { await this.close(); throw error; }
  }
  async readLoop() {
    try {
      while (this.reader) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.framer.push(value);
      }
      this.active?.fail(new Error('Serial connection ended'));
    } catch { this.active?.fail(new Error('Serial read failed')); }
  }
  command(command, { accept = line => /^OK(?:\s|$)/.test(line), dataPrefix, timeout = 15000, sensitive = false } = {}) {
    if (/[\r\n\0]/.test(command)) return Promise.reject(new Error('Command contains a forbidden line break'));
    const work = this.tail.then(async () => {
      if (!this.connected) throw new Error('Connect a console first');
      if (sensitive) this.quiet = true;
      else if (!/^save restore/i.test(command)) this.log(`> ${command}`);
      return new Promise((resolve, reject) => {
        const finish = (fn, value) => {
          clearTimeout(timer); this.active = null; fn(value);
        };
        const timer = setTimeout(() => {
          finish(reject, new Error('Command timed out; reconnect before retrying'));
          // A late OK must never satisfy a subsequent command.
          void this.close();
        }, timeout);
        const request = { accept, dataPrefix, sensitive, done: result => finish(resolve, result), fail: error => finish(reject, error) };
        this.active = request;
        this.writer.write(new TextEncoder().encode(command + '\n')).catch(() => {
          if (this.active === request) request.fail(new Error('Serial write failed'));
        });
      });
    });
    this.tail = work.catch(() => {});
    return work;
  }
  async close() {
    // Invalidate the session before yielding so queued commands cannot write
    // into a closing port after a timeout or disconnect.
    const port = this.port;
    this.port = null;
    this.active?.fail(new Error('Console disconnected'));
    try { await this.reader?.cancel(); } catch {}
    await this.readTask;
    try { this.reader?.releaseLock(); } catch {}
    try { this.writer?.releaseLock(); } catch {}
    this.reader = null; this.writer = null; this.port = null; this.readTask = null;
    try { if (port?.readable || port?.writable) await port.close(); } catch {}
    this.framer = new LineFramer(line => this.line(line));
    this.quiet = false;
  }
}
