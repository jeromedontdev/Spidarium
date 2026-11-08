import test from 'node:test';
import assert from 'node:assert/strict';
import { PREY, preySprite, freshState, restoreSim, feed, care, pounce, purchaseGift, visitGift, startGame, answerGame, tick } from '../lib/sim.js';
import { access } from 'node:fs/promises';

test('feed cap is 3 and a pounce removes prey, fills food and awards Silk/XP', () => {
  const state = freshState();
  for (let i = 0; i < 3; i++) feed(state, 'fly', () => 0);
  assert.equal(state.prey.length, 3); assert.match(feed(state, 'moth'), /maximum 3/);
  const { x, y } = state.prey[0]; const result = pounce(state, x, y);
  assert.equal(result.caught, true); assert.equal(state.prey.length, 2); assert.equal(state.meals, 1);
  assert.equal(state.hunger, 88); assert.equal(state.silk, 29); assert.equal(state.xp, 15);
});
test('out-of-range pounce is clamped, energy gates jumping and sleep blocks feed', () => {
  const state = freshState(); pounce(state, 2000, 2000);
  assert.ok(Math.hypot(state.x - 240, state.y - 240) <= 186.01);
  state.energy = 1; assert.equal(pounce(state, 50, 50).caught, false);
  care(state, 'sleep'); assert.match(feed(state, 'fly'), /Wake/);
  const before = state.energy; tick(state, 2); assert.ok(state.energy > before);
});
test('care counts actions and gift ownership charges once, with functional visits', () => {
  const state = freshState(); care(state, 'clean');
  assert.equal(state.hygiene, 100); assert.equal(state.cleanCount, 1); assert.equal(state.name, '');
  assert.throws(() => care(state, 'pet'));
  const before = state.silk; purchaseGift(state, 0); assert.equal(state.silk, before - 16);
  purchaseGift(state, 0); assert.equal(state.silk, before - 16); assert.deepEqual(state.owned, [0]);
  state.energy = 50; visitGift(state); assert.equal(state.visits, 1); assert.equal(state.energy, 62);
  assert.match(purchaseGift(state, 17), /Unlocks/);
});
test('all three games finish exactly eight rounds and perfect games reward progression', () => {
  for (const kind of ['memory', 'reflex', 'hunt']) {
    const state = freshState(), game = startGame(kind, () => .5);
    for (let round = 0; round < 8; round++) {
      assert.equal(answerGame(state, game, 2, () => .5), true);
      assert.equal(game.finished, round === 7);
    }
    assert.equal(state.games, 1); assert.equal(state.silk, 48); assert.equal(state.xp, 24);
    assert.throws(() => answerGame(state, game, 2));
  }
});
test('failed answers do not award wins; leveling is derived from XP', () => {
  const state = freshState(), game = startGame('hunt', () => 0);
  for (let round = 0; round < 8; round++) answerGame(state, game, round < 6 ? 0 : -1, () => 0);
  assert.equal(game.correct, 6); assert.equal(state.silk, 36); assert.equal(state.xp, 18);
  for (let i = 0; i < 5; i++) care(state, 'clean');
  assert.equal(state.level, 2);
});
test('browser restore sanitizes storage and never consumes device save blobs', () => {
  assert.deepEqual(restoreSim({ version: 99 }), freshState());
  const state = restoreSim({ version: 2, hunger: -10, energy: 999, xp: 85, level: 999, owned: [0, 0, 99, '1'], gift: 99, prey: Array(50).fill({}) });
  assert.equal(state.hunger, 0); assert.equal(state.energy, 100); assert.equal(state.level, 3);
  assert.deepEqual(state.owned, [0]); assert.equal(state.gift, null); assert.equal(state.prey.length, 0);
});
test('legacy preview identity is removed, current custom browser names remain separate from device names', () => {
  assert.equal(restoreSim({ version: 1, name: 'Old preview name', xp: 42 }).name, '');
  assert.equal(restoreSim({ version: 2, name: 'My spider', xp: 42 }).name, 'My spider');
});
test('all eight catches display their authentic dead PNG and expire without consuming prey slots', async () => {
  for (const species of PREY) {
    const state = freshState();
    feed(state, species, () => 0);
    const target = state.prey[0];
    assert.equal(pounce(state, target.x, target.y).caught, true);
    assert.equal(state.prey.length, 0);
    assert.equal(state.splatters[0].species, species);
    assert.equal(preySprite(species, true), `prey_${species}_dead_0`);
    await access(new URL(`../../assets/${preySprite(species, true)}.png`, import.meta.url));
    tick(state, 1); assert.equal(state.splatters.length, 1);
    tick(state, 1); assert.equal(state.splatters.length, 0);
    feed(state, species, () => 0); pounce(state, target.x, target.y);
    care(state, 'clean'); assert.equal(state.splatters.length, 0);
  }
});
test('each Journal achievement uses an existing achievement-specific source PNG', async () => {
  for (const name of ['first_meal', 'first_clean', 'first_play', 'first_hunt', 'ten_meals', 'level_5', 'adult', 'first_perfect']) {
    await access(new URL(`../../assets/achievement_${name}.png`, import.meta.url));
  }
});
