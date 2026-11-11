import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { DeviceGame } from '../lib/device-ui.js';
import { Minigame, COLOR_RGB } from '../lib/minigames.js';
import { SPECIES } from '../lib/sim.js';

function frame(kind) {
  const engine = new Minigame(kind, 0, () => 0);
  const tiles = Array.from({ length: kind === 2 ? 1 : 4 }, () => ({
    style: {}, dataset: {}, lastChild: {}, children: [],
    replaceChildren() { this.children = []; },
    append(child) { this.children.push(child); },
  }));
  return { game: { engine, tiles, prompt: {}, target: kind === 0 ? {} : null,
    bar: { firstChild: { style: {} } } } };
}

test('Settings Back returns to the root so care and games remain reachable', () => {
  const opened = [];
  const ui = { open: name => opened.push(name) };
  DeviceGame.prototype.back.call(ui, 'Sound');
  DeviceGame.prototype.back.call(ui, 'Settings');
  assert.deepEqual(opened, ['Settings', 'Menu']);
});

test('Bug Hunt uses the refreshed walk artwork for all eight target and decoy species', () => {
  const previous = globalThis.document;
  globalThis.document = { createElement: () => ({}) };
  try {
    const ui = frame(0);
    for (let species = 0; species < SPECIES.length; species++) {
      ui.game.engine.bug = species;
      ui.game.engine.bugs = Array.from({ length: 4 }, (_, i) => (species + i) % SPECIES.length);
      DeviceGame.prototype.gameFrame.call(ui, 0);
      assert.ok(ui.game.target.src.endsWith(`/assets/prey_${SPECIES[species].key}_walk_0.png`));
      for (let i = 0; i < 4; i++) {
        const tile = ui.game.tiles[i];
        const bug = SPECIES[ui.game.engine.bugs[i]];
        assert.equal(tile.style.background, '#24405a');
        assert.equal(tile.children.length, 1);
        assert.ok(tile.children[0].src.endsWith(`/assets/prey_${bug.key}_walk_0.png`));
        assert.ok(tile.children[0].alt.length);
      }
    }
  } finally {
    if (previous === undefined) delete globalThis.document;
    else globalThis.document = previous;
  }
});

test('Color Match preserves native finite signal colors through preview and active play', async () => {
  const native = await readFile(new URL('../../main/ui_menu.c', import.meta.url), 'utf8');
  const palette = native.match(/k_play_rgb\[\] = \{([^}]+)\}/)[1]
    .split(',').map(value => value.trim().replace('0x', '#'));
  assert.deepEqual(COLOR_RGB, palette);
  const ui = frame(1);
  DeviceGame.prototype.gameFrame.call(ui, 0);
  assert.ok(ui.game.tiles.every(tile => tile.disabled && tile.lastChild.textContent === '...'));
  DeviceGame.prototype.gameFrame.call(ui, 750);
  ui.game.tiles.forEach((tile, i) => {
    assert.equal(tile.disabled, false);
    assert.equal(tile.style.background, COLOR_RGB[ui.game.engine.colors[i]]);
    assert.notEqual(tile.lastChild.textContent, '...');
  });
});

test('Quick Tap keeps distinct wait, feint, and green states on pixel chrome', () => {
  const ui = frame(2), engine = ui.game.engine;
  for (const [feints, ready, color, label] of [
    [0, false, '#24405a', 'WAIT'], [1, false, '#846421', 'HOLD'],
    [2, false, '#b94646', 'NOT YET'], [2, true, '#296e55', 'Tap!'],
  ]) {
    engine.feints = feints; engine.ready = ready;
    DeviceGame.prototype.gameFrame.call(ui, 0);
    assert.equal(ui.game.tiles[0].style.background, color);
    assert.equal(ui.game.tiles[0].lastChild.textContent, label);
  }
});
