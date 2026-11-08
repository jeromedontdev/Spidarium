import test from 'node:test';
import assert from 'node:assert/strict';
import { LocalProgress, STORAGE_KEY } from '../lib/local-progress.js';
import { Minigame } from '../lib/minigames.js';
import { soundSamples } from '../lib/device-audio.js';

test('native sound durations, deterministic noise and PCM range match', () => {
  for (const [name, duration] of [['eat', 140], ['pounce', 90], ['molt', 260], ['tap', 60], ['rustle', 280], ['chirp', 160], ['drop', 220]]) {
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
