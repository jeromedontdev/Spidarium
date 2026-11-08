export const PREY = ['fly', 'cricket', 'moth', 'beetle', 'roach', 'mealworm', 'fruitfly', 'waxworm'];
export function preySprite(species, dead = false, frame = 0) {
  if (!PREY.includes(species)) throw new Error('Unknown prey');
  return `prey_${species}_${dead ? 'dead_0' : `walk_${frame % 2}`}`;
}
export const BACKGROUNDS = ['moss', 'fern', 'cork', 'leaf_litter', 'orchid', 'sand', 'woodland', 'minimal'];
export const GIFTS = [
  ['Resting Stone', 1, 'energy', 'Rest and recover'], ['Leaf Shelter', 2, 'hygiene', 'A quiet place to groom'],
  ['Jumping Branch', 3, 'mood', 'Practice little jumps'], ['Acorn Hide', 4, 'energy', 'Explore, then rest'],
  ['Moss Patch', 5, 'hygiene', 'A soft, clean hiding place'], ['Sap Trail', 6, 'mood', 'Follow the scent'],
  ['Flower Lure', 8, 'hunger', 'Inspect the flowers'], ['Crystal Lookout', 10, 'mood', 'Watch the world'],
  ['Amber Hearth', 12, 'energy', 'Warm and sleepy'], ['Coral Scrubber', 14, 'hygiene', 'A grooming station'],
  ['Silk Hammock', 16, 'energy', 'Rock gently to sleep'], ['Bone Tunnel', 18, 'mood', 'Explore a tunnel'],
  ['Pearl Toy', 20, 'mood', 'Play with a pearl'], ['Ruby Launchpad', 23, 'mood', 'Hop from a launchpad'],
  ['Sapphire Lookout', 26, 'mood', 'An excellent lookout'], ['Emerald Garden', 30, 'hygiene', 'Fresh green comfort'],
  ['Diamond Retreat', 35, 'energy', 'Rest in a retreat'], ['Royal Perch', 40, 'mood', 'Survey the kingdom'],
].map(([name, level, stat, effect], id) => ({ id, name, level, stat, effect, cost: 12 + level * 4 }));
const clamp = (value, min = 0, max = 100) => Math.max(min, Math.min(max, value));
export const freshState = () => ({
  version: 2, name: '', hunger: 68, mood: 82, energy: 78, hygiene: 90,
  silk: 24, xp: 0, level: 1, meals: 0, catches: 0, games: 0, bestGame: 0, cleanCount: 0,
  x: 240, y: 257, state: 'idle', prey: [], splatters: [], gift: null, owned: [], visits: 0,
  background: 'moss', muted: true, brightness: 85, battery: 78, charging: false,
});
export function restoreSim(raw) {
  const base = freshState();
  if (!raw || ![1, 2].includes(raw.version)) return base;
  for (const key of ['hunger', 'mood', 'energy', 'hygiene', 'brightness', 'battery']) {
    if (Number.isFinite(raw[key])) base[key] = clamp(raw[key]);
  }
  for (const key of ['silk', 'xp', 'meals', 'catches', 'games', 'bestGame', 'cleanCount', 'visits']) {
    if (Number.isSafeInteger(raw[key]) && raw[key] >= 0) base[key] = Math.min(raw[key], 1000000);
  }
  base.level = 1 + Math.floor(base.xp / 40);
  if (raw.version === 2 && typeof raw.name === 'string') base.name = raw.name.slice(0, 16);
  if (BACKGROUNDS.includes(raw.background)) base.background = raw.background;
  base.muted = raw.muted !== false; base.charging = raw.charging === true;
  base.owned = Array.isArray(raw.owned) ? [...new Set(raw.owned.filter(id => Number.isInteger(id) && GIFTS[id]))] : [];
  if (base.owned.includes(raw.gift)) base.gift = raw.gift;
  return base;
}
export function award(state, xp, silk) {
  state.xp += xp; state.silk += silk; state.level = 1 + Math.floor(state.xp / 40);
}
export function feed(state, species, random = Math.random) {
  if (state.state === 'sleep') return 'Wake your spider before feeding.';
  if (!PREY.includes(species)) throw new Error('Unknown prey');
  if (state.prey.length >= 3) return 'The tank is full: catch a bug first (maximum 3).';
  const angle = random() * Math.PI * 2, radius = 70 + random() * 100;
  state.prey.push({ species, x: 240 + Math.cos(angle) * radius, y: 240 + Math.sin(angle) * radius, phase: random() * 6 });
  return `${species} added. Drag your spider toward it, then release to pounce.`;
}
export function care(state, action) {
  if (action === 'sleep') { state.state = state.state === 'sleep' ? 'idle' : 'sleep'; return state.state === 'sleep' ? 'Dreaming in silk. Energy is recovering.' : 'Your spider is awake.'; }
  if (state.state === 'sleep') return 'Your spider is asleep. Wake them first.';
  if (action === 'clean') { state.hygiene = 100; state.splatters = []; state.cleanCount++; award(state, 5, 2); return 'Fresh moss, happy spider. +2 Silk'; }
  throw new Error('Unknown care action');
}
export function pounce(state, x, y) {
  if (state.state === 'sleep') return { caught: false, message: 'Your spider is asleep.' };
  if (state.energy < 8) return { caught: false, message: 'Too sleepy to jump. Try resting.' };
  const dx = x - state.x, dy = y - state.y;
  const distance = Math.hypot(dx, dy);
  const range = 165;
  if (distance > range) { x = state.x + dx / distance * range; y = state.y + dy / distance * range; }
  const radial = Math.hypot(x - 240, y - 240);
  if (radial > 186) { x = 240 + (x - 240) / radial * 186; y = 240 + (y - 240) / radial * 186; }
  state.x = x; state.y = y; state.energy = clamp(state.energy - 6);
  const index = state.prey.findIndex(bug => Math.hypot(bug.x - x, bug.y - y) < 42);
  if (index >= 0) {
    const [caught] = state.prey.splice(index, 1);
    state.splatters.push({ species: caught.species, x: caught.x, y: caught.y, remaining: 2 });
    state.hunger = clamp(state.hunger + 20); state.mood = clamp(state.mood + 5);
    state.meals++; state.catches++; award(state, 15, 5); state.state = 'eat';
    return { caught: true, message: 'A perfect little lunch. +5 Silk · +15 XP' };
  }
  state.state = 'pounce'; return { caught: false, message: 'Almost! Aim at a bug and release. Jump range: 165 px.' };
}
export function purchaseGift(state, id) {
  const gift = GIFTS[id];
  if (!gift) throw new Error('Unknown gift');
  if (!state.owned.includes(id)) {
    if (state.level < gift.level) return `Unlocks at level ${gift.level}.`;
    if (state.silk < gift.cost) return `Need ${gift.cost} Silk. Earn it through care, hunting and games.`;
    state.silk -= gift.cost; state.owned.push(id);
  }
  state.gift = id;
  return `${gift.name} placed. Click it in the tank to invite your spider to visit.`;
}
export function visitGift(state) {
  if (state.gift === null) return 'Give your spider a gift first.';
  if (state.state === 'sleep') return 'Your spider is asleep.';
  const gift = GIFTS[state.gift];
  state.x = 315; state.y = 330; state[ gift.stat ] = clamp(state[gift.stat] + 12); state.visits++;
  award(state, 2, 0); return `${gift.effect}. ${gift.stat} +12.`;
}
export function tick(state, seconds) {
  seconds = clamp(seconds, 0, 2);
  state.splatters = state.splatters.filter(splatter => (splatter.remaining -= seconds) > 0);
  state.hunger = clamp(state.hunger - seconds * .06);
  state.hygiene = clamp(state.hygiene - seconds * .04);
  state.mood = clamp(state.mood - seconds * .03);
  state.energy = clamp(state.energy + seconds * (state.state === 'sleep' ? 2.5 : -.04));
  if (state.state !== 'sleep' && state.hunger < 15) state.mood = clamp(state.mood - seconds * .15);
}
export function startGame(kind, random = Math.random) {
  if (!['memory', 'reflex', 'hunt'].includes(kind)) throw new Error('Unknown game');
  return { kind, round: 0, correct: 0, finished: false, target: Math.floor(random() * 4) };
}
export function answerGame(state, game, answer, random = Math.random) {
  if (game.finished) throw new Error('Game already finished');
  const hit = answer === game.target;
  if (hit) game.correct++;
  game.round++;
  if (game.round === 8) {
    game.finished = true; state.games++; state.bestGame = Math.max(state.bestGame, game.correct); state.mood = clamp(state.mood + game.correct * 2);
    award(state, game.correct * 3, game.correct * 2 + (game.correct === 8 ? 8 : 0));
  } else game.target = Math.floor(random() * 4);
  return hit;
}
