import test from 'node:test';
import assert from 'node:assert/strict';
import { LocalProgress, STORAGE_KEY } from '../lib/local-progress.js';
import { Minigame, GAME_NAMES } from '../lib/minigames.js';
import { soundSamples } from '../lib/device-audio.js';
import { readFile } from 'node:fs/promises';

test('native sound durations, deterministic noise and PCM range match', () => {
  for (const [name, duration] of [['eat', 140], ['pounce', 90], ['molt', 260], ['tap', 60], ['rustle', 8000], ['chirp', 160], ['drop', 10000]]) {
    const pcm = soundSamples(name);
    assert.equal(pcm.length, duration * 16);
    assert.equal(Math.abs(pcm[0]), 0);
    assert.ok(pcm.every(sample => Number.isFinite(sample) && Math.abs(sample) < 1));
  }
  assert.deepEqual(soundSamples('rustle'), soundSamples('rustle'));
  assert.throws(() => soundSamples('bad'), /Unknown/);
});

test('progress survives reload and corrupted saves are not silently overwritten', () => {
  const entries = new Map(), errors = [];
  const storage = { getItem: key => entries.get(key) ?? null, setItem: (key, value) => entries.set(key, value) };
  const restore = value => value ?? { xp: 0 };
  const save = new LocalProgress(storage, restore, text => errors.push(text));
  save.save({ xp: 281, name: 'Spider', autoFeed: true, autoHunt: false });
  assert.deepEqual(new LocalProgress(storage, restore, text => errors.push(text)).load(),
    { xp: 281, name: 'Spider', autoFeed: true, autoHunt: false });
  entries.set(STORAGE_KEY, '{bad');
  const broken = new LocalProgress(storage, restore, text => errors.push(text));
  broken.load();
  assert.equal(broken.save({ xp: 0 }), false);
  assert.equal(entries.get(STORAGE_KEY), '{bad');
  assert.match(errors[0], /not been overwritten/);
});
test('quota failure is surfaced and reset does not pretend to succeed', () => {
  const errors = [], store = { getItem: () => null, setItem: () => { throw new Error('quota'); } };
  const progress = new LocalProgress(store, value => value, text => errors.push(text));
  assert.equal(progress.save({ xp: 2 }), false);
  assert.equal(progress.reset({ xp: 0 }), false);
  assert.match(errors.join(' '), /quota/);
});
test('all eight native difficulty ramps and pending delays match', () => {
  for (let kind = 0; kind < 3; kind++) {
    const game = new Minigame(kind, 0, () => 0);
    for (let round = 0; round < 8; round++) {
      game.round = round; game.startRound(1000);
      assert.equal(game.duration, kind === 2 ? 620 - round * 45 : 2000 - round * 110);
      assert.equal(game.length, 1 + Math.floor(round / 3));
      assert.equal(game.shiftAt, 2500 - round * 60);
      game.endRound(false, 1000);
      if (round < 7) assert.equal(game.deadline, 1600);
    }
  }
});
test('memory preview/ordered inputs and duplicate guard match native timing', () => {
  const game = new Minigame(1, 0, () => 0);
  game.round = 7; game.startRound(0);
  assert.equal(game.answerAt(0, 10), false);
  game.tick(750); game.tick(1500); game.tick(2250);
  assert.equal(game.preview, false);
  assert.equal(game.answerAt(game.answer, 2260), true);
  assert.equal(game.answerAt(game.answer, 2261), false);
  game.answerAt(game.answer, 2410); game.answerAt(game.answer, 2560);
  assert.equal(game.finished, true);
  assert.equal(game.score, 1);
});
test('reaction feints reject early taps, cancel never pays a reward', () => {
  const early = new Minigame(2, 0, () => 0);
  early.answerAt(0, 100);
  assert.equal(early.score, 0); assert.equal(early.pending, true);
  const game = new Minigame(2, 0, () => 0);
  game.round = 2; game.startRound(0);
  game.tick(1100); assert.equal(game.feints, 1);
  game.tick(1600); assert.equal(game.feints, 2);
  game.tick(2100); assert.equal(game.ready, true);
  game.cancel(); game.answerAt(0, 2110);
  assert.equal(game.score, 0); assert.equal(game.finished, false);
});
test('moving hunt target keeps the chosen species and enforces deadline', () => {
  const game = new Minigame(0, 0, () => 0);
  const before = game.answer;
  game.tick(1500);
  assert.notEqual(game.answer, before);
  assert.equal(game.bugs[game.answer], game.bug);
  game.answerAt(game.answer, 2600);
  assert.equal(game.score, 0);
});

test('web weave lights a growing order and rejects a wrong strand', () => {
  /* The spider-themed memory game: four anchors lit in order, tapped back in
   * order. It must never repeat an anchor back to back, because a doubled tap
   * reads as one tap and the sequence becomes impossible to follow. */
  const game = new Minigame(3, 0, () => 0);
  for (let round = 0; round < 8; round++) {
    game.round = round; game.startRound(1000);
    assert.equal(game.length, Math.min(4, 2 + Math.floor(round / 2)));
    for (let i = 1; i < game.length; i++)
      assert.notEqual(game.weave[i], game.weave[i - 1], 'no doubled anchor');
    /* The order is shown before input: startRound leaves the game previewing,
     * and only the preview ticks clear it. */
    assert.equal(game.preview, true, 'the order is shown before input');
    for (let i = 0; i < game.length; i++) game.tick(game.deadline);
    assert.equal(game.preview, false);
    assert.equal(game.weaveStep, 0);
    /* A wrong anchor ends the round with no score. The tap has to land INSIDE
     * the window, or the round would end on the timeout instead and the anchor
     * comparison would never be exercised. The last round finishes the game
     * instead of going pending, so only rounds 0..6 are pending. */
    const wrong = (game.weave[0] + 1) % 4;
    game.answerAt(wrong, game.deadline - 1);
    assert.equal(game.pending, round < 7);
    assert.equal(game.score, 0);
    game.deadline = 0; game.tick(0);
  }
});

test('web weave scores a correct full sequence', () => {
  const game = new Minigame(3, 0, () => 0);
  game.round = 0; game.startRound(1000);
  for (let i = 0; i < game.length; i++) game.tick(game.deadline);
  /* Inside the window, so the sequence is what decides the round. */
  const at = game.deadline - 1;
  for (let i = 0; i < game.length; i++) {
    game.inputGuard = 0;
    game.answerAt(game.weave[i], at);
  }
  assert.equal(game.score, 1, 'a full correct sequence scores');
  assert.equal(game.pending, true, 'round 0 of 8 goes pending, not finished');
});

test('pounce target stays inside the panel and shrinks each round', () => {
  /* The spider's own verb: a bug appears and the player taps it in time. */
  const game = new Minigame(4, 0, () => 0);
  let previous = Infinity;
  for (let round = 0; round < 8; round++) {
    game.round = round; game.startRound(1000);
    assert.ok(game.pounceSize >= 36 && game.pounceSize <= 64,
      `size ${game.pounceSize} stays tappable`);
    assert.ok(game.pounceSize <= previous, 'the target never grows');
    previous = game.pounceSize;
    assert.ok(game.pounceX > 12 && game.pounceX < 456 - 12, 'inside the panel horizontally');
    assert.ok(game.pounceY > 148 && game.pounceY < 480 - 100, 'clear of the prompt and bar');
    assert.equal(game.ready, true);
    assert.equal(game.pounce(game.deadline - 1), true);
    assert.equal(game.score, round + 1, 'an in-time tap scores');
    game.deadline = 0; game.tick(0);
  }
});

test('pounce misses when the window closes', () => {
  const game = new Minigame(4, 0, () => 0);
  game.round = 0; game.startRound(1000);
  const deadline = game.deadline;
  game.tick(deadline);
  assert.equal(game.pending, true, 'the round ends on timeout');
  assert.equal(game.score, 0);
});

test('all five games are named and reachable', () => {
  assert.deepEqual(GAME_NAMES, ['Bug Hunt', 'Color Match', 'Quick Tap', 'Web Weave', 'Pounce']);
  for (let kind = 0; kind < GAME_NAMES.length; kind++) {
    const game = new Minigame(kind, 0, () => 0);
    assert.equal(game.kind, kind);
    assert.equal(typeof game.prompt(['Bug']), 'string');
  }
  assert.throws(() => new Minigame(5, 0, () => 0), /Unknown minigame/);
});

test('brand marks and favicon are vector art, not platform emoji glyphs', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  const css = await readFile(new URL('../style.css', import.meta.url), 'utf8');
  const icon = await readFile(new URL('../icon.svg', import.meta.url), 'utf8');
  // A text glyph renders as whatever emoji font the platform picks, which looks
  // wrong on mobile. Every mark must be real vector art.
  assert.equal(html.includes('\u2733'), false, 'no emoji glyph in the markup');
  assert.equal((html.match(/class="brand-mark"/g) ?? []).length, 2, 'header and footer marks');
  assert.equal((html.match(/class="marquee-mark"/g) ?? []).length, 3, 'marquee separators');
  assert.match(html, /<link rel="icon" href="\.\/icon\.svg" type="image\/svg\+xml">/);
  assert.match(html, /<link rel="apple-touch-icon"/);
  // The marks are sized by CSS now that they are SVG, not by font-size.
  assert.match(css, /\.brand-mark\{[^}]*width:32px;height:32px/);
  assert.match(css, /\.marquee-mark\{[^}]*width:9px;height:9px/);
  // Every .brand-mark rule must size the SVG, not a font glyph. A leftover
  // font-size override would silently do nothing to an SVG.
  const markRules = css.match(/\.brand-mark\{[^}]*\}/g) ?? [];
  assert.ok(markRules.length >= 1, 'brand-mark rules exist');
  for (const rule of markRules) {
    assert.match(rule, /width:\d+px;height:\d+px/, `sized by width/height: ${rule}`);
    assert.equal(/font-size/.test(rule), false, `no font sizing: ${rule}`);
  }  // The icon is a self-contained SVG with a viewBox so it scales cleanly.
  assert.match(icon, /^<svg xmlns="http:\/\/www\.w3\.org\/2000\/svg" viewBox="0 0 32 32"/);
  assert.match(icon, /aria-label="Spidarium"/);
  assert.equal(/<image|href=/.test(icon), false, 'no raster or external reference');
});
