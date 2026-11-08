import test from 'node:test';
import assert from 'node:assert/strict';
import { DeviceGame } from '../lib/device-ui.js';

test('screenshot downloads only the playfield canvas and retains its menu', async t => {
  const link = { click() { this.clicked = true; }, remove() { this.removed = true; } };
  globalThis.document = { createElement: () => link };
  t.after(() => { delete globalThis.document; });
  const notices = [];
  const game = {
    modal: { name: 'Settings' }, hud: true, draws: 0,
    draw() { this.draws++; },
    canvas: { toBlob(callback, type) {
      assert.equal(type, 'image/png');
      callback(new Blob(['PNG pixels'], { type }));
    } },
    root: { append(value) { assert.equal(value, link); } },
    report(...args) { notices.push(args); },
  };
  const menu = game.modal;
  await DeviceGame.prototype.screenshot.call(game);
  assert.equal(game.draws, 1);
  assert.equal(game.modal, menu);
  assert.equal(game.hud, true);
  assert.match(link.href, /^blob:/);
  assert.match(link.download, /^spidarium-\d+\.png$/);
  assert.equal(link.clicked, true);
  assert.equal(link.removed, true);
  assert.equal(notices.length, 1);
  assert.match(notices[0][0], /Menus and HUD are excluded/);
});

test('PNG encoding failure reports an error rather than download success', async () => {
  const notices = [];
  await DeviceGame.prototype.screenshot.call({
    draw() {}, canvas: { toBlob(callback) { callback(null); } },
    report(...args) { notices.push(args); },
  });
  assert.deepEqual(notices, [['Screenshot failed: PNG encoding failed', true]]);
});
