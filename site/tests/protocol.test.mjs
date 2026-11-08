import test from 'node:test';
import assert from 'node:assert/strict';
import { LineFramer, SerialConsole } from '../lib/protocol.js';

const encode = text => new TextEncoder().encode(text);
class FakePort {
  constructor(reply) { this.reply = reply; this.commands = []; this.readable = null; this.writable = null; this.closed = false; }
  async open() {
    this.readable = new ReadableStream({ start: controller => { this.controller = controller; } });
    this.writable = new WritableStream({ write: bytes => {
      const command = new TextDecoder().decode(bytes).trimEnd(); this.commands.push(command); this.reply(command, text => this.controller.enqueue(encode(text)));
    } });
  }
  async close() { this.closed = true; this.readable = null; this.writable = null; }
}
const help = (command, send) => { if (command === 'help') { send('OK commands:\n help list\n'); setTimeout(() => send(' reboot restart\n'), 5); return true; } return false; };
test('reader frames fragmented CRLF, ANSI and UTF-8 without duplicate or partial lines', () => {
  const lines = [], framer = new LineFramer(line => lines.push(line));
  framer.push(encode('\x1b[32mSAVE HE')); framer.push(encode('X abc\r\nOK save export\r\n'));
  const unicode = encode('Spider é\n'); framer.push(unicode.subarray(0, 8)); framer.push(unicode.subarray(8));
  assert.deepEqual(lines, ['SAVE HEX abc', 'OK save export', 'Spider é']);
});
test('framer rejects unbounded serial frames', () => { assert.throws(() => new LineFramer(() => {}, 4).push(encode('12345'))); });
test('help is drained before serialized commands, unrelated OK cannot satisfy framed export', async () => {
  const port = new FakePort((command, send) => {
    if (help(command, send)) return;
    if (command === 'save export') { send('OK heartbeat\nSAVE HEX ab'); setTimeout(() => send('cd\nOK save export\n'), 4); }
    else if (command === 'mem') send('MEM free=123\nOK mem\n');
  });
  const session = new SerialConsole({ readyDelay: 0 });
  await session.connect(port);
  const exportPromise = session.command('save export', { dataPrefix: 'SAVE HEX ', accept: line => line === 'OK save export' });
  const memPromise = session.command('mem', { accept: line => line === 'OK mem' });
  assert.equal((await exportPromise).data, 'abcd'); assert.equal((await memPromise).line, 'OK mem');
  assert.deepEqual(port.commands, ['help', 'save export', 'mem']); await session.close(); assert.equal(port.closed, true);
});
test('ERR and missing framed data explicitly reject commands', async () => {
  const port = new FakePort((command, send) => { if (!help(command, send)) send(command === 'bad' ? 'ERR invalid\n' : 'OK save export\n'); });
  const session = new SerialConsole({ readyDelay: 0 }); await session.connect(port);
  await assert.rejects(session.command('bad'), /ERR invalid/);
  await assert.rejects(session.command('save export', { dataPrefix: 'SAVE HEX ', accept: line => line === 'OK save export' }), /without framed/);
  await session.close();
});
test('credential commands and all sensitive echoes/async logs remain absent from activity', async () => {
  const logs = [];
  const port = new FakePort((command, send) => {
    if (help(command, send)) return;
    send(`echo ${command}\nconnected SSID private-home\nOK wifi connected private-home\n`);
  });
  const session = new SerialConsole({ readyDelay: 0, log: line => logs.push(line) }); await session.connect(port);
  await session.command('wifi add private-home|very-secret', { sensitive: true, accept: line => /^OK wifi/.test(line) });
  session.line('private-home asynchronous radio log');
  assert.equal(logs.join('').includes('private-home'), false); assert.equal(logs.join('').includes('very-secret'), false);
  assert.equal(session.quiet, true); await session.close();
});
test('timeout disconnects before late OK can satisfy a subsequent command', async () => {
  const port = new FakePort((command, send) => { help(command, send); });
  const session = new SerialConsole({ readyDelay: 0 }); await session.connect(port);
  await assert.rejects(session.command('silent', { timeout: 10 }), /timed out/);
  await new Promise(resolve => setTimeout(resolve, 5));
  assert.equal(session.connected, false); assert.equal(port.closed, true);
  await assert.rejects(session.command('mem'), /Connect/);
});
test('command injection is refused before serial writes', async () => {
  const session = new SerialConsole(); await assert.rejects(session.command('mem\nreboot'), /forbidden/);
});
test('already queued command cannot race a timeout into a closing transport', async () => {
  const port = new FakePort((command, send) => { help(command, send); });
  const session = new SerialConsole({ readyDelay: 0 }); await session.connect(port);
  const first = session.command('silent', { timeout: 10 });
  const queued = session.command('mem');
  const results = await Promise.allSettled([first, queued]);
  assert.equal(results[0].status, 'rejected');
  assert.match(results[0].reason.message, /timed out/);
  assert.equal(results[1].status, 'rejected');
  assert.match(results[1].reason.message, /Connect/);
  assert.deepEqual(port.commands, ['help', 'silent']);
  await session.close();
});
