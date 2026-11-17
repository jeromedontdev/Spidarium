import { companionDefaults, companionValid, companionTick, companionEvent } from './companion.js?v=1.1.31';
export * from './companion.js?v=1.1.31';
import { lifeDefaults, lifeValid, lifeTick, lifeContext,
  lifeRetire as retireLifePet, lifeResume as resumeLifePet } from './life.js?v=1.1.31';
export * from './life.js?v=1.1.31';
import { CLIMB_MASK, CLIMB_MASK_ROWS, CLIMB_MASK_COUNT } from './climb-mask.js?v=1.1.31';
/**
 * Native flat persistent fields plus browser-only runtime `_engine`, `prey`,
 * `poop`, and `spider` rendering view. Coordinates are native 480x480 pixels.
 * tick(state, seconds=.05) accumulates 50ms steps; menus can set paused=true.
 * freshState/restoreSim accept {random:()=>number, now:()=>milliseconds}.
 * Actions mutate state and return booleans; pounce queues a flight, not a catch.
 * lastEvent reports catch/miss/rejection, with no browser-only economy.
 */
export const CONFIG = Object.freeze({
  LOGIC_HZ:20, RENDER_HZ:30, GAME_TIME_SCALE:1, LOGIC_PERIOD_MS:50,
  TICKS_PER_DAY:2880, AGE_TICK_MS:30000, DECAY_PERIOD_MS:3000, SAVE_PERIOD_MS:5000,
  HUNGER_PER_MIN:3, HYGIENE_PER_MIN:.7, ENERGY_PER_MIN:.5,
  NEEDS_AGE_SCALE_PER_STAGE:.18, NEEDS_AGE_SCALE_MAX:1.45,
  ENERGY_REGEN_ASLEEP_PER_MIN:6, HYGIENE_REGEN_PER_MIN:2, POOP_LIFETIME_MS:600000,
  MOOD_LOW_THRESHOLD:25, FEED_HUNGER_GAIN:15, HUNGER_FULL:95, FEED_MOOD_GAIN:8,
  FEED_HYGIENE_COST:12, CLEAN_XP_MAX:3, CLEAN_SCORE_FULL:24, CLEAN_HYGIENE_SET:100,
  BUG_POOP_MS:150000, POOP_DIRT_SPIDER:12, POOP_DIRT_BUG:6,
  // Grazing: a bug that is not fleeing stops to feed for a moment, then moves
  // on. Mirrors PREY_GRAZE_* in main/game_config.h.
  PREY_GRAZE_FIRST_MS:3000, PREY_GRAZE_MIN_MS:900, PREY_GRAZE_MAX_MS:2200,
  PREY_GRAZE_GAP_MIN_MS:2500, PREY_GRAZE_GAP_MAX_MS:6000,
  PLAY_MOOD_GAIN:10, PLAY_ENERGY_COST:5,
  PLAY_XP:4, PET_MOOD_GAIN:6, PET_XP:2, EAT_MS:800, SLEEP_MIN_MS:6000,
  PET_RADIUS_PX:56, PET_COOLDOWN_MS:1200, PET_XP_DAILY_MAX:10, PET_REACT_MS:520,
  MOVE_SPEED:55, TURN_RATE_DEG:540, WANDER_RETARGET_MS:2000, ARRIVE_RADIUS:12,
  PREY_SPEED:30, PREY_FLEE_SPEED:70, PREY_AGGRO_RADIUS:70, PREY_AGGRO_HYSTERESIS:1.25,
  WILD_SPAWN_MS:1200000, WILD_HUNGER_GAIN:8, WILD_MOOD_GAIN:4,
  WILD_FLEE_RADIUS_MULT:1.25, WILD_FLEE_SPEED_MULT:1.15,
  PREY_SPEED_PER_LEVEL:.06, PREY_SPEED_MAX_MULT:1.8, PREY_ALERT_PER_LEVEL:.05,
  PREY_ALERT_MAX_MULT:1.6, PREY_FLEE_BURST_MS:300, PREY_FLEE_REST_MS:1000,
  PREY_FLEE_RADIUS_MAX:102, FLY_RETARGET_MS:600, FLY_JITTER:.35,
  CRICKET_HOP_CYCLE_MS:900, CRICKET_HOP_BURST_MS:200, CRICKET_HOP_SPEED_MULT:2.2,
  MOTH_FLUTTER_HZ:.006, MOTH_FLUTTER_PX:14, XP_ACHIEVEMENT:15, VISITOR_XP:20,
  MOOD_REFUSE_HUNT:20, MOOD_HUNGER_OVERRIDE:40, BOND_PER_CATCH:2, BOND_PER_PERFECT:4,
  BOND_PER_PET:1, BOND_SICK_DECAY:1, BOND_TURN_TO_TAP:25, BOND_AIM_BONUS:50,
  BOND_AIM_BONUS_MULT:1.15, BOND_GIFT:75, BOND_SWAY:100,
  CATCH_BASE:.55, CATCH_PER_PX:.004, CATCH_ENERGY_BONUS:.2, CATCH_STAGE_BONUS:.1,
  AIM_RANGE_PX:110, AIM_WINDOW_MS:2600, AIM_WINDOW_MIN_MS:1600, AIM_WINDOW_PER_LEVEL:20,
  POUNCE_MAX_PX:160, PLAYER_POUNCE_BONUS:1.15, AUTO_POUNCE_PENALTY:.55,
  AIM_MIN_PULL_PX:18, AIM_POWER_PERFECT:.1, AIM_POWER_GOOD:.22,
  CATCH_PERFECT_PX:30, CATCH_GOOD_PX:54, SPIDER_BODY_R_PX:22, TAP_ASSIST_PX:64,
  XP_PERFECT:18, XP_GOOD:10, XP_MISS:1, PERFECT_HUNGER_BONUS:5, PERFECT_MOOD_BONUS:4,
  STREAK_BONUS_PER:.05, STREAK_MAX:10, POUNCE_ENERGY_COST:3, EAT_ENERGY_GAIN:6,
  STAGE_JUVENILE_DAYS:2, STAGE_ADULT_DAYS:5, MOLT_DAYS:2, XP_PER_LEVEL:50,
  XP_DAY:10, XP_MOLT:8, PLAY_MARGIN:34, MAX_PREY:3, MAX_POOP:8, STRIDE_PX:24,
  DAILY_STREAK_MAX:7, DAILY_XP_BASE:6, DAILY_XP_PER_DAY:2, DAILY_SILK_BASE:4,
  DAILY_SILK_PER_DAY:2, DAILY_TASK_XP:12, DAILY_TASK_SILK:8, DAILY_TASK_COUNT:4,
  DAILY_NEVER:0xFFFFFFFF,
  TREAT_COUNT:12, TREAT_HUNGER_COST:6, TREAT_MOOD_COST:8, TREAT_ENERGY_COST:10,
  TREAT_HUNGER_GAIN:25, TREAT_MOOD_GAIN:30, TREAT_ENERGY_GAIN:35,
});const C = CONFIG, PI = Math.PI, TAU = PI*2, U32 = 0xffffffff;
export const STATES = Object.freeze({IDLE:0,WANDER:1,STALK:2,AIM:3,POUNCE:4,EAT:5,SLEEP:6,PLAY:7,MOLT:8,SICK:9,PERCH:10});
export const STATE_NAMES = ['idle','wander','stalk','aim','pounce','eat','sleep','play','molt','sick','perch'];
// Forward-axis compression while perched on a small prop. Mirrors
// PERCH_FORESHORTEN in main/cspider.c.
export const PERCH_FORESHORTEN = 0.72;
// There is deliberately NO perspective shift while climbing, and the constants
// that drove one are gone rather than left dormant.
//
// Three attempts were made: a forward-axis compression, a body tilt following
// the surface gradient, and a full dorsal projection that turned the top-down
// sprite into a side view. Each was individually plausible and each looked
// wrong on the panel, because a 96x96 pixel-art sprite drawn from above cannot
// be re-projected into a convincing profile by moving its parts around. The
// silhouette is authored top-down; skewing it produces a distorted top-down
// spider, not a side view.
//
// What remains is the honest part of the effect: the spider is LIFTED up the
// structure, with its contact shadow under its feet, so it visibly stands on
// something with height. Mirrors main/cspider.c.
export const STAGES = ['Spiderling','Juvenile','Adult'];
export const TEMPERAMENTS = [
  {name:'bold',aim_window_mult:.85,catch_mult:1.1,speed_mult:1.1,hunger_decay_mult:1,sleep_threshold:15},
  {name:'timid',aim_window_mult:1.25,catch_mult:.95,speed_mult:.9,hunger_decay_mult:.9,sleep_threshold:20},
  {name:'greedy',aim_window_mult:1,catch_mult:1.05,speed_mult:1,hunger_decay_mult:1.25,sleep_threshold:15},
  {name:'lazy',aim_window_mult:1.15,catch_mult:.9,speed_mult:.8,hunger_decay_mult:.85,sleep_threshold:30},
];
export const SPECIES = [
  ['fly',1,'erratic',1,1,1,1,1,1,1],['cricket',2,'hop',1,1,1,1,1.2,1.15,1.1],
  ['moth',4,'flutter',1,1,1,1,1.1,1.3,1],
  ['beetle',6,'slow',.55,1,1,.5,1.6,1.1,1.3],
  ['roach',10,'wander',.75,1.25,.85,.85,1.5,1,1.2],
  ['mealworm',13,'slow',.3,.4,.65,1.1,1.9,1.2,1.4],
  ['fruitfly',16,'erratic',1.25,1.2,1.1,.85,.7,1.4,.8],
  ['waxworm',20,'slow',.2,.3,.55,1.2,2.4,1.5,1.6],
].map(([name,unlock_level,move,speed_mult,flee_speed_mult,flee_radius_mult,perfect_mult,
        hunger_mult,mood_mult,energy_mult],id)=>
  Object.freeze({id,key:name,name:name==='fruitfly'?'fruit fly':name,unlock_level,level:unlock_level,move,
    speed_mult,flee_speed_mult,flee_radius_mult,perfect_mult,hunger_mult,mood_mult,energy_mult}));
export const PREY = SPECIES.map(s=>s.key);
export function preySprite(species,dead=false,frame=0) {
  const id = speciesId(species);
  if (id<0) throw new Error('Unknown prey');
  return `prey_${PREY[id]}_${dead?'dead_0':`walk_${frame%2}`}`;
}
export const BACKGROUNDS = ['cork','moss','leaf_litter','sand','fern','orchid','woodland','minimal',
  'mushroom','gothic','seashell','succulent','willow','desert','crystal','cemetery',
  'house','dark_cave','night','autumn','snow','pond','ruins','marsh',
  'fairy_glade','moonflower_grove','cathedral','rose_courtyard','library','observatory',
  'kitchen','sewing_room','witch_garden','moon_altar','primeval_jungle','amber_basin',
  'neon_alley','circuit_garden','lunar_base','starship','enchanted_court','dragon_heath',
  'lost_city','sunken_temple','zen_garden','bamboo_grove','sakura_courtyard','cloud_sanctuary'];
export const BACKGROUND_COSTS = [0,12,12,16,20,24,28,16,32,40,28,24,32,36,48,48,
  52,60,60,54,58,64,72,68,
  64,72,80,74,62,78,54,58,68,82,72,76,84,88,90,96,86,92,94,98,70,74,78,100];
export function backgroundOwned(s,id) {
  return Number.isInteger(id)&&id>=0&&id<BACKGROUNDS.length&&
    !!((id<32?s.backgrounds_owned:s.backgrounds_owned_extra)&(1<<(id%32)));
}
export function buyBackground(s,id) {
  if(!Number.isInteger(id)||!BACKGROUNDS[id])return event(s,'rejected',{reason:'unknown background'});
  const cost=backgroundOwned(s,id)?0:BACKGROUND_COSTS[id];
  if(s.silk<cost)return event(s,'rejected',{reason:'not enough Silk'});
  s.silk-=cost;
  const field=id<32?'backgrounds_owned':'backgrounds_owned_extra';
  s[field]=(s[field]|(1<<(id%32)))>>>0;s.background=BACKGROUNDS[id];return true;
}
export function borderOwned(s,id){
  return Number.isInteger(id)&&id>=0&&id<BACKGROUNDS.length&&
    !!((id<32?s.borders_owned:s.borders_owned_extra)&(1<<(id%32)));
}
export function borderCost(id){return BACKGROUNDS[id]?10+Math.floor(BACKGROUND_COSTS[id]/2):0;}
export function buyBorder(s,id){
  if(id!==255&&(!Number.isInteger(id)||!BACKGROUNDS[id]))
    return event(s,'rejected',{reason:'unknown border'});
  const cost=id===255||borderOwned(s,id)?0:borderCost(id);
  if(s.silk<cost)return event(s,'rejected',{reason:'not enough Silk'});
  s.silk-=cost;s.border_id=id;
  if(id!==255){
    const field=id<32?'borders_owned':'borders_owned_extra';
    s[field]=(s[field]|(1<<(id%32)))>>>0;
  }
  return true;
}
const bonusKeys = ['energy_regen_mult','hygiene_decay_mult','pounce_range_mult','aim_window_mult','wild_spawn_bonus',
  'pounce_success_mult','movement_efficiency_mult','mood_mult','fear_mult','prey_flee_penalty',
  'catch_success_mult','prey_detection_range_mult','all_stats_decay_mult','bonus_synergy_mult'];
const giftRows = [
  ['pebble','Resting Stone',1,'rests faster',{energy_regen_mult:1.5}],
  ['leaf','Leaf Shelter',2,'stays cleaner',{hygiene_decay_mult:.6}],
  ['twig','Jumping Branch',3,'leaps further',{pounce_range_mult:1.2}],
  ['acorn','Acorn Hide',4,'builds energy',{energy_regen_mult:1.3,all_stats_decay_mult:.95}],
  ['moss','Moss Patch',5,'natural camouflage',{pounce_success_mult:1.15,prey_flee_penalty:.85}],
  ['sap','Sap Trail',6,'uses less energy',{movement_efficiency_mult:1.1}],
  ['flower','Flower Lure',8,'more wild bugs',{wild_spawn_bonus:1}],
  ['crystal','Crystal Lookout',10,'longer aim window',{aim_window_mult:1.25}],
  ['amber','Amber Hearth',12,'warmth bonus',{mood_mult:1.1,all_stats_decay_mult:.95}],
  ['coral','Coral Scrubber',14,'less grime',{hygiene_decay_mult:.8}],
  ['silk','Silk Hammock',16,'steadier catches',{catch_success_mult:1.2}],
  ['bone','Bone Tunnel',18,'calmer prey',{fear_mult:.7}],
  ['pearl','Pearl Toy',20,'iridescent glow',{mood_mult:1.1}],
  ['ruby','Ruby Launchpad',23,'aggressive bonus',{pounce_range_mult:1.25,catch_success_mult:1.15}],
  ['sapphire','Sapphire Lookout',26,'clarity',{aim_window_mult:1.15,prey_detection_range_mult:1.2}],
  ['emerald','Emerald Garden',30,'vitality',{energy_regen_mult:1.05,all_stats_decay_mult:.95}],
  ['diamond','Diamond Retreat',35,'perfection',{bonus_synergy_mult:1.5}],
  ['crown','Royal Perch',40,'ruler vivarium',{bonus_synergy_mult:2}],
  ['fairy_cottage','Fairy Cottage',4,'rests faster',{energy_regen_mult:1.4}],
  ['moon_bed','Moon Bed',6,'lifts mood',{mood_mult:1.15}],
  ['reading_nook','Reading Nook',8,'longer aim window',{aim_window_mult:1.2}],
  ['teapot_hide','Teapot Hide',3,'stays cleaner',{hygiene_decay_mult:.7}],
  ['witch_hat','Witch Hat',10,'calmer hunting',{fear_mult:.75}],
  ['fern_nest','Fern Nest',7,'builds energy',{energy_regen_mult:1.35}],
  ['neon_den','Neon Den',12,'uses less energy',{movement_efficiency_mult:1.15}],
  ['star_capsule','Star Capsule',14,'stays cleaner',{hygiene_decay_mult:.75}],
  ['dragon_roost','Dragon Roost',16,'leaps further',{pounce_range_mult:1.2}],
  ['sun_temple','Sun Temple',18,'watches prey',{prey_detection_range_mult:1.25}],
  ['zen_pavilion','Zen Pavilion',5,'lifts mood',{mood_mult:1.15}],
  ['petal_swing','Petal Swing',9,'lifts mood',{mood_mult:1.2}],
  ['velvet_throne','Velvet Throne',13,'builds energy',{energy_regen_mult:1.3}],
  ['scholar_perch','Scholar Perch',11,'longer aim window',{aim_window_mult:1.2}],
  ['potion_hide','Potion Hide',15,'slower fleeing prey',{prey_flee_penalty:.85}],
  ['fossil_cradle','Fossil Cradle',17,'rests faster',{energy_regen_mult:1.4}],
  ['holo_garden','Holo Garden',20,'slower needs decay',{all_stats_decay_mult:.95}],
  ['torii_retreat','Torii Retreat',22,'balanced bonuses',{bonus_synergy_mult:1.15}],
];
const interactions=['Resting','Hiding','Climbing','Hiding','Exploring','Exploring','Watching','Watching','Resting','Grooming','Resting','Exploring','Playing','Climbing','Watching','Exploring','Resting','Climbing',
  'Resting','Resting','Watching','Hiding','Hiding','Resting','Exploring','Resting','Climbing','Watching',
  'Resting','Playing','Resting','Watching','Hiding','Resting','Exploring','Resting'];
const giftEffects=[
  'Rest here; energy recovers 50% faster',
  'Hide under leaves; grime falls 40% slower',
  'Climb and practice; jump range +20%',
  'Explore a hide; energy recovery +30%',
  'Hide in moss; fleeing prey move 15% slower',
  'Explore the trail; movement costs less energy',
  'Watch flowers; attracts more wild prey',
  'Watch from a perch; aim time +25%',
  'Rest by amber; mood gain +10%',
  'Groom here; grime falls 20% slower',
  'Rest in silk; catch margin +20%',
  'Explore the tunnel; calmer hunting',
  'Play with a pearl; mood gain +10%',
  'Practice jumps; range +25%, catch margin +15%',
  'Watch prey; detection +20%, aim time +15%',
  'Explore a garden; all needs decay 5% slower',
  'Rest in a retreat; balanced bonuses +20%',
  'Climb a royal perch; balanced bonuses +30%',
  'Rest in a cottage; energy recovery +40%',
  'Rest under the moon; mood gain +15%',
  'Read in peace; aim time +20%',
  'Hide in porcelain; grime falls 30% slower',
  'Hide under a hat; calmer hunting',
  'Rest in ferns; energy recovery +35%',
  'Explore neon; movement costs less energy',
  'Rest in a capsule; grime falls 25% slower',
  'Practice at a roost; jump range +20%',
  'Watch from a temple; detection +25%',
  'Rest in a pavilion; mood gain +15%',
  'Play on petals; mood gain +20%',
  'Rest on velvet; energy recovery +30%',
  'Watch from books; aim time +20%',
  'Hide with potions; fleeing prey 15% slower',
  'Rest in a cradle; energy recovery +40%',
  'Explore a hologram; all needs decay 5% slower',
  'Rest under torii; balanced bonuses +15%',
];
export const GIFTS = giftRows.map(([key,name,level,effect,bonuses],id)=>Object.freeze({
  id,key,name,level,unlock_level:level,effect:giftEffects[id],cost:12+level*4,asset:`gift_${key}`,interaction:interactions[id],bonuses:Object.freeze(bonuses),
}));
export const ACHIEVEMENTS = [
  ['first_meal','First Meal','Catch and eat one prey'],
  ['first_pet','First Pet','Tap the spider gently'],
  ['first_clean','First Clean','Clean a dirty enclosure'],
  ['first_play','First Play','Finish one minigame'],
  ['first_hunt','First Hunt','Land a successful pounce'],
  ['first_molt','First Molt','Reach age two days'],
  ['ten_meals','Ten Meals','Eat ten prey'],
  ['adult','Adulthood','Reach age five days'],
  ['level_5','Level 5','Earn 200 total XP'],
  ['survived_sick','Survived Sickness','Recover with food and mood'],
  ['first_perfect','Perfect Hunt','Catch prey inside the aim ring'],
  ['ten_cleans','Tidy Keeper','Clean ten dirty enclosures','ui_clean'],
  ['ten_plays','Playmate','Finish ten minigames','ui_play'],
  ['twenty_five_meals','Hungry Hunter','Eat twenty-five prey','ui_feed'],
  ['bond_50','Trusted Friend','Reach bond fifty','ui_mood'],
  ['streak_3','Regular Visitor','Claim three days in a row','ui_daily'],
  ['decor_3','Interior Designer','Place three decorations','ui_decor'],
  ['backgrounds_4','Scene Collector','Own four backgrounds','ui_backgrounds'],
  ['first_border','Framed','Buy an enclosure border','ui_display'],
  ['first_gift','Gift Giver','Place a gift in the enclosure','ui_treats'],
].map(([id,name,goal,asset],index)=>Object.freeze({id,bit:1<<index,name,goal,asset:asset??`achievement_${id}`,xp:15,silk:20}));
/* Columns: name, want, weight, hunger<, energy<, mood<, hygiene<, bond>,
 * prey, period, climbable, gift.
 *
 * The last three columns are what make the spider feel like it has a LIFE
 * rather than a stat readout: it notices the time of day, the things you have
 * put in its tank, and the gift you gave it. A rule whose condition cannot be
 * satisfied (no clock, no climbable decor, no gift) simply never fires, so the
 * table degrades cleanly on a fresh tank. Mirrors k_mind_rules in mind.c. */
export const MIND_RULES = [
  ['starving','food',100,25,-1,-1,-1,-1,-1,-1,-1,-1],
  ['exhausted','rest',95,-1,20,-1,-1,-1,-1,-1,-1,-1],
  ['hungry','food',70,60,-1,-1,-1,-1,1,-1,-1,-1],
  ['peckish','explore',50,60,-1,-1,-1,-1,0,-1,-1,-1],
  ['sad','play',60,-1,-1,25,-1,-1,-1,-1,-1,-1],
  ['grimy','clean',55,-1,-1,-1,25,-1,-1,-1,-1,-1],
  ['drowsy','rest',45,-1,70,-1,-1,-1,-1,3,-1,-1],
  ['dusk','explore',35,-1,-1,-1,-1,-1,-1,2,-1,-1],
  ['basking','sun',32,-1,-1,-1,-1,-1,-1,0,-1,-1],
  ['content','watch',30,-1,-1,60,-1,50,-1,-1,-1,-1],
  ['restless','climb',28,-1,55,45,-1,-1,-1,-1,1,-1],
  ['perching','perch',26,-1,40,40,-1,-1,-1,-1,-1,-1],
  ['fond','visit',24,-1,30,50,-1,20,-1,-1,-1,1],
  ['curious','explore',20,-1,-1,-1,-1,-1,-1,-1,-1,-1],
  ['weary','rest',15,-1,35,-1,-1,-1,-1,-1,-1,-1],
  ['shy','hide',14,-1,-1,-1,-1,10,-1,-1,-1,-1],
  ['fidget','perch',8,-1,-1,-1,-1,-1,-1,-1,-1,-1],
  ['idle','nothing',1,-1,-1,-1,-1,-1,-1,-1,-1,-1],
  ['reserved0','nothing',-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],
  ['reserved1','nothing',-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],
];
const services = new WeakMap();
const clamp = (v,min=0,max=100)=>Math.max(min,Math.min(max,v));
const wrap = a=>{while(a>PI)a-=TAU;while(a<-PI)a+=TAU;return a;};
const integer = (v,fallback=0,max=U32)=>Number.isSafeInteger(v)&&v>=0?Math.min(v,max):fallback;
const round = n=>n>=0?Math.floor(n+.5):Math.ceil(n-.5);
const f32 = Math.fround;
const multiply = (...values)=>values.reduce((a,b)=>f32(a*f32(b)),1);
const field = {left:34,right:446,top:34,bottom:446};
function random(s){return clamp((services.get(s)?.random??Math.random)(),0,1-Number.EPSILON);}
function roll(s,n){return Math.floor(random(s)*0x80000000)%n;}
function temperament(s){return TEMPERAMENTS[s.temperament]??TEMPERAMENTS[0];}
/** Needs decay faster as the spider grows. Capped so an old spider stays playable. */
export function needsAgeScale(s){
  return Math.min(1+(s.stage??0)*C.NEEDS_AGE_SCALE_PER_STAGE,C.NEEDS_AGE_SCALE_MAX);
}
function speciesId(v){return Number.isInteger(v)&&SPECIES[v]?v:PREY.indexOf(v==='fruit fly'?'fruitfly':v);}
function runtime(s){return s._engine;}
const companionNow = s => services.get(s)?.now?.() ?? Date.now();
const companionEnsure = s => companionTick(s,companionNow(s));
const companionNote = (s,kind,value=255) => companionEvent(s,kind,value,companionNow(s));
export function giftBonus(s) {
  const b=Object.fromEntries(bonusKeys.map(k=>[k,k==='wild_spawn_bonus'?0:1]));
  if(!s.gift.used)return b;
  Object.assign(b,GIFTS[s.gift.kind]?.bonuses);
  let synergy=b.bonus_synergy_mult;
  if(synergy>1.3)synergy=s.gift.kind===16?1.2:1.3;
  for(const k of bonusKeys) {
    if(['wild_spawn_bonus','bonus_synergy_mult'].includes(k))continue;
    b[k]*=['hygiene_decay_mult','fear_mult','prey_flee_penalty','all_stats_decay_mult'].includes(k)?1/synergy:synergy;
  }
  return b;
}
function engineDefaults(now=0) {
  return {now_ms:now,last_decay:now,last_age:now,next_mind:now,mind_since:now,remainder:0,hunger_acc:0,hygiene_acc:0,energy_acc:0,
    px:240,py:240,idle_until:0,retarget_at:0,wander_dir:0,target:null,drag:null,
    aim_start:0,aim_active:false,aim_power:0,aim_ideal:0,pounce:null,next_pounce_at:0,
    sleep_forced:false,sleep_start:0,oneshot_until:0,gift_visit_at:0,gift_hold_until:0,
    gift_visit_end:0,gift_visiting:false,wild_seeded:false,last_wild:now,last_visitor_day:U32,
    next_bond_gift:0,last_pet:0,petted_once:false,pet_react_until:0,stuck_x:0,stuck_y:0,stuck_at:0,gait_x:240,gait_y:240,gait_valid:false,gait_moved_at:0,
 perch_x:240,perch_y:240,perch_until:0,perch_heading:null,perch_cooldown_until:0,
    tap_heading:null};
}
export function freshState(options={}) {
  const now=options.now?.()??0;
  const s={version:21,life:lifeDefaults(),companion:companionDefaults(),hunger:65,mood:60,energy:80,hygiene:80,age_ticks:0,meals:0,pets:0,cleans:0,plays:0,
    xp:0,seed:0xc0ffee,spider_x:240,spider_y:240,heading_deg:0,gift:{kind:0,variant:0,used:0,x:0,y:0},gift_path:0,
    stage:0,sick:0,muted:0,gift_mode:0,gift_selected:0,level:1,achievements:0,days_lived:0,last_molt_day:0,
    state:0,taps:0,eat_until_ms:0,blink_window:0,blink_until:0,spider_heading:0,streak:0,best_streak:0,
    temperament:0,bond:0,name:'',colour:0,pupil_colour:0,leg_colour:2,fang_colour:1,
    silk:20,gifts_owned:0,brightness:50,auto_feed:0,auto_hunt:0,volume:70,last_seen:0,background:'cork',
    backgrounds_owned:1,backgrounds_owned_extra:0,catalogue_owned_high:0,
    borders_owned:0,borders_owned_extra:0,border_id:255,
    power_mode:0,screen_timeout:3,deep_sleep_after:0,rtc_wake:0,
    daily_day:0xFFFFFFFF,daily_base_day:0xFFFFFFFF,daily_task_base:0,daily_streak:0,daily_task_done:0,
    daily_override:0xFFFFFFFF,pet_xp_day:0xFFFFFFFF,pet_xp_today:0,
    decor_owned:0,decor_owned_extra:0,music_enabled:1,lights_dimmed:0,music_volume:100,music_track:0,usb_sounds:1,
    decor_locked:0,auto_clean:0,climb_owned:0,
    decor:Array.from({length:DECOR_SLOTS},()=>({kind:DECOR_EMPTY,blocking:0,x:0,y:0})),
    prey:[],poop:[],paused:false,setup_complete:false,thought:'',mind_rule:null,lastEvent:null,visits:0,
    spider:{x:240,y:240,heading:0,angle:0,state:0,gait_phase:0,walking:false},_engine:engineDefaults(now)};
  services.set(s,options);
  s.temperament=options.temperament??roll(s,4);
  // Camel-case controls are aliases, not duplicate persistent sources of truth.
  Object.defineProperties(s,{
    autoFeed:{enumerable:false,get(){return !!this.auto_feed;},set(v){this.auto_feed=v?1:0;}},
    autoHunt:{enumerable:false,get(){return !!this.auto_hunt;},set(v){this.auto_hunt=v?1:0;}},
    x:{enumerable:true,get(){return this.spider.x;},set(v){this.spider_x=round(v);this.spider.x=this._engine.px=v;}},
    y:{enumerable:true,get(){return this.spider.y;},set(v){this.spider_y=round(v);this.spider.y=this._engine.py=v;}},
    heading:{enumerable:true,get(){return this.spider_heading;},set(v){this.spider_heading=v;this.spider.heading=v;}},
    gait_phase:{enumerable:true,get(){return this.spider.gait_phase;},set(v){this.spider.gait_phase=v;}},
    theme:{get(){return this.background;},set(v){this.background=v;}},
    sound:{get(){return !this.muted;},set(v){this.muted=v?0:1;}},
    bodyColour:{get(){return this.colour;},set(v){this.colour=v;}},
    eyeColour:{get(){return this.pupil_colour;},set(v){this.pupil_colour=v;}},
    legColour:{get(){return this.leg_colour;},set(v){this.leg_colour=v;}},
    fangColour:{get(){return this.fang_colour;},set(v){this.fang_colour=v;}},
    now_ms:{get(){return this._engine.now_ms;}},
    gift_visit:{get(){return this._engine.gift_visiting;}},
    aim_x:{get(){return this._engine.drag?.x??nearest(this)?.prey.x??this.x;}},
    aim_y:{get(){return this._engine.drag?.y??nearest(this)?.prey.y??this.y;}},
  });
  return s;
}
export function validateProgress(raw) {
  const fail=message=>{throw new Error(`Invalid browser progress: ${message}`);};
  const plain=v=>v!==null&&typeof v==='object'&&(Object.getPrototypeOf(v)===Object.prototype||Object.getPrototypeOf(v)===null);
  if(!plain(raw))fail('expected a plain save object');
  const seen=new WeakSet();
  const inspect=(v,path)=>{
    if(typeof v==='number'&&!Number.isFinite(v))fail(`${path} is not finite`);
    if(v===null||['string','number','boolean'].includes(typeof v))return;
    if(typeof v!=='object'||(!Array.isArray(v)&&!plain(v)))fail(`${path} is not JSON save data`);
    if(seen.has(v))fail(`${path} contains cyclic metadata`);
    seen.add(v);
    for(const [key,value] of Object.entries(v))inspect(value,`${path}.${key}`);
    seen.delete(v);
  };
  inspect(raw,'save');
  if(![1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21].includes(raw.version))fail('unsupported version');
  if(raw.version<17 && plain(raw.life)) {
    const life={...raw.life};
    if(!Object.hasOwn(life,'reserved') && !Object.hasOwn(life,'light_gesture_disabled'))
      fail('life lighting byte is missing');
    if(Object.hasOwn(life,'reserved') && life.reserved!==0)fail('life reserved byte is invalid');
    if(life.light_gesture_disabled!==undefined && life.light_gesture_disabled!==0)
      fail('legacy light gesture flag is invalid');
    delete life.reserved;
    life.light_gesture_disabled=0;
    raw={...raw,life};
  }
  if(raw.version>=16&&!lifeValid(raw.life))fail('life is invalid');
  if(raw.life!==undefined&&!lifeValid(raw.life))fail('life is invalid');
  if(raw.version>=15&&!companionValid(raw.companion))fail('companion is invalid');
  if(raw.companion!==undefined&&!companionValid(raw.companion))fail('companion is invalid');
  const legacy=raw.version<3;
  const required=legacy?['hunger','mood','energy','hygiene','xp','silk']:
    ['hunger','mood','energy','hygiene','xp','silk','name','level','stage','age_ticks','days_lived',
      'achievements','gifts_owned','gift','spider_x','spider_y','spider_heading','temperament',
      'bond','colour','pupil_colour','leg_colour','fang_colour','auto_feed','auto_hunt','setup_complete'];
  for(const key of required)if(!Object.hasOwn(raw,key))fail(`missing ${key}`);
  const number=(key,min,max,whole=true)=>{
    if(!Object.hasOwn(raw,key))return;
    const v=raw[key];
    if(typeof v!=='number'||!Number.isFinite(v)||(whole&&!Number.isSafeInteger(v))||v<min||v>max)fail(`${key} is outside its valid range`);
  };
  for(const key of ['hunger','mood','energy','hygiene'])
    number(key,legacy?-Number.MAX_SAFE_INTEGER:0,legacy?Number.MAX_SAFE_INTEGER:100,!legacy);
  for(const key of ['xp','silk','age_ticks','days_lived','last_molt_day','meals','pets','cleans','plays','taps','seed',
    'visits','cleanCount','catches','games','bestGame'])number(key,0,U32);
  number('level',1,U32);number('stage',0,2);number('temperament',0,3);number('bond',0,100);
  number('achievements',0,(1<<ACHIEVEMENTS.length)-1);number('gifts_owned',0,raw.version>=12?U32:(1<<18)-1);
  number('streak',0,65535);number('best_streak',0,65535);
  for(const key of ['colour','bodyColour'])number(key,0,5);
  for(const key of ['pupil_colour','leg_colour','fang_colour','eyeColour','legColour','fangColour'])number(key,0,8);
  for(const key of ['auto_feed','auto_hunt','sick','decor_locked','auto_clean'])number(key,0,1);
  for(const key of ['autoFeed','autoHunt','paused','setup_complete','sound','charging'])
    if(Object.hasOwn(raw,key)&&typeof raw[key]!=='boolean')fail(`${key} must be boolean`);
  if(Object.hasOwn(raw,'muted')&&![0,1,false,true].includes(raw.muted))fail('muted must be boolean or 0/1');
  for(const key of ['background','theme'])
    if(Object.hasOwn(raw,key)&&(!BACKGROUNDS.includes(raw[key])||
        (raw.version<12&&BACKGROUNDS.indexOf(raw[key])>=24)))fail(`${key} is unknown`);
  number('brightness',legacy?0:10,100,!legacy);number('battery',0,100,false);
  number('volume',0,100);
  number('backgrounds_owned',1,raw.version>=12?U32:0xffffff);
  if(raw.version>=12) {
    number('backgrounds_owned_extra',0,65535);number('catalogue_owned_high',0,255);
    number('borders_owned',0,U32);number('borders_owned_extra',0,65535);
    if(raw.border_id!==undefined&&raw.border_id!==255&&!borderOwned(raw,raw.border_id))
      fail('selected border must be owned');
  }
  number('music_enabled',0,1);number('lights_dimmed',0,1);
  number('music_volume',0,100);number('music_track',0,raw.version>=14?9:2);
  if(raw.version>=15){
    if(!Object.hasOwn(raw,'usb_sounds'))fail('missing usb_sounds');
    number('usb_sounds',0,1);
  }
  number('power_mode',0,2);number('screen_timeout',0,4);
  number('deep_sleep_after',0,4);number('rtc_wake',0,2);
  if(raw.version>=9&&(!Number.isInteger(raw.backgrounds_owned)||
      !backgroundOwned(raw,BACKGROUNDS.indexOf(raw.background??raw.theme))))
    fail('selected background must be owned');
  number('daily_day',0,U32);number('daily_base_day',0,U32);
  number('daily_task_base',0,U32);number('daily_streak',0,255);number('daily_task_done',0,1);
  number('pet_xp_day',0,U32);number('pet_xp_today',0,255);
  number('decor_owned',0,U32);number('decor_owned_extra',0,raw.version>=12?U32:3);
  if(Object.hasOwn(raw,'decor')){
    if(!Array.isArray(raw.decor)||raw.decor.length<DECOR_SLOTS)fail('decor must be an array of slots');
    for(const item of raw.decor){
      if(!item||typeof item!=='object')fail('decor slot is invalid');
      if(!Number.isSafeInteger(item.kind)||(item.kind!==DECOR_EMPTY&&(item.kind<0||item.kind>=(raw.version>=12?DECOR_KINDS.length:34))))
        fail('decor kind is outside its valid range');
      if(!Number.isInteger(item.blocking)||item.blocking<0||item.blocking>(raw.version>=19?3:raw.version>=13?2:1))
        fail('decor path mode is invalid');
      if(!Number.isSafeInteger(item.x)||!Number.isSafeInteger(item.y))fail('decor position is invalid');
    }
  }
  for(const key of ['spider_x','spider_y'])number(key,34,446);
  for(const key of ['x','y'])number(key,legacy?-Number.MAX_SAFE_INTEGER:34,legacy?Number.MAX_SAFE_INTEGER:446,false);
  for(const key of ['spider_heading','heading','gait_phase','now_ms'])
    number(key,key==='now_ms'?0:-Number.MAX_VALUE,Number.MAX_VALUE,false);
  if(Object.hasOwn(raw,'name')&&(typeof raw.name!=='string'||raw.name.includes('\0')||raw.name.length>(legacy?16:15)))fail('name is invalid');
  if(Object.hasOwn(raw,'createdAt')&&
    !((typeof raw.createdAt==='number'&&Number.isFinite(raw.createdAt)&&raw.createdAt>=0)||
      (typeof raw.createdAt==='string'&&Number.isFinite(Date.parse(raw.createdAt)))))fail('createdAt is invalid');
  const validGift=id=>Number.isInteger(id)&&id>=0&&id<(raw.version>=12?GIFTS.length:18);
  if(Object.hasOwn(raw,'owned')&&(!Array.isArray(raw.owned)||raw.owned.some(id=>!validGift(id))))fail('owned contains an invalid gift');
  if(Object.hasOwn(raw,'gift')){
    const g=raw.gift;
    if(legacy&&(g===null||validGift(g))){}
    else if(!plain(g)||![0,1].includes(g.used)||!validGift(g.kind)||
      !Number.isSafeInteger(g.variant)||g.variant<0||g.variant>255||
      !Number.isFinite(g.x)||!Number.isFinite(g.y)||
      (g.used&&(g.x<34||g.x>446||g.y<34||g.y>446)))fail('gift is invalid');
  }
  if(Object.hasOwn(raw,'state')&&!(legacy?typeof raw.state==='string'&&STATE_NAMES.includes(raw.state):Number.isInteger(raw.state)&&raw.state>=0&&raw.state<=10))fail('state is invalid');
  for(const key of ['prey','poop','splatters'])if(Object.hasOwn(raw,key)&&!Array.isArray(raw[key]))fail(`${key} must be an array`);
  if(Object.hasOwn(raw,'_engine')){
    if(!plain(raw._engine))fail('_engine metadata must be an object');
    for(const [key,initial] of Object.entries(engineDefaults()))
      if(Object.hasOwn(raw._engine,key)&&initial!==null&&typeof raw._engine[key]!==typeof initial)
        fail(`_engine.${key} has an invalid type`);
    if(!Number.isFinite(raw._engine.now_ms)||raw._engine.now_ms<0)fail('_engine.now_ms is invalid');
  }
  if(Object.hasOwn(raw,'spider')){
    if(!plain(raw.spider)||!Number.isFinite(raw.spider.x)||!Number.isFinite(raw.spider.y)||
      !Number.isFinite(raw.spider.heading)||!Number.isFinite(raw.spider.gait_phase))fail('spider metadata is invalid');
  }
  if(!legacy){
    for(const p of raw.prey??[])
      if(!plain(p)||!Number.isInteger(p.species)||!SPECIES[p.species]||
        typeof p.alive!=='boolean'||typeof p.dead!=='boolean'||!Number.isFinite(p.x)||!Number.isFinite(p.y))
        fail('prey metadata is invalid');
    for(const p of raw.poop??[])
      if(!plain(p)||!Number.isFinite(p.x)||!Number.isFinite(p.y)||!Number.isFinite(p.expires)||
        (p.dirt!==undefined&&!Number.isFinite(p.dirt)))
        fail('poop metadata is invalid');
  }
  return raw;
}
export function restoreSim(raw,options={}) {
  const s=freshState(options);
  if(raw===null||raw===undefined)return s;
  raw=validateProgress(raw);
  if(raw.version>=16)s.life=JSON.parse(JSON.stringify(raw.life));
  if(raw.version>=15)s.companion=JSON.parse(JSON.stringify(raw.companion));
  for(const k of ['hunger','mood','energy','hygiene','bond'])if(Number.isFinite(raw[k]))s[k]=Math.trunc(clamp(raw[k]));
  for(const k of ['age_ticks','meals','pets','cleans','plays','xp','seed','achievements','days_lived','last_molt_day','silk','gifts_owned','taps','visits'])
    s[k]=integer(raw[k],s[k]);
  s.cleans=integer(raw.cleans,integer(raw.cleanCount,s.cleans));
  s.plays=integer(raw.plays,integer(raw.games,s.plays));
  // Old preview used 40 XP/level. Preserve its earned level and fractional
  // progress when migrating to the device's 50 XP/level economy.
  if(raw.version===1||raw.version===2)s.xp=Math.min(U32,Math.floor(s.xp*1.25));
  s.name=typeof raw.name==='string'?raw.name.slice(0,15):'';
  s.setup_complete=raw.setup_complete===true||Boolean(raw.createdAt)||(raw.version<3&&Boolean(raw.name));
  for(const k of ['colour','pupil_colour','leg_colour','fang_colour'])s[k]=integer(raw[k],s[k],255);
  for(const [old,key] of [['bodyColour','colour'],['eyeColour','pupil_colour'],['legColour','leg_colour'],['fangColour','fang_colour']])
    if(Number.isInteger(raw[old]))s[key]=integer(raw[old],s[key],255);
  s.temperament=integer(raw.temperament,s.temperament,3);s.stage=integer(raw.stage,0,2);
  s.streak=integer(raw.streak,0,65535);s.best_streak=integer(raw.best_streak,0,65535);
  // The daily loop arrived in v6. A v5 save has no daily fields, so they keep
  // the fresh-state sentinels and the first tick snapshots a baseline.
  s.daily_day=integer(raw.daily_day,s.daily_day,U32);
  s.daily_base_day=integer(raw.daily_base_day,s.daily_base_day,U32);
  s.daily_task_base=integer(raw.daily_task_base,0,U32);
  s.daily_streak=integer(raw.daily_streak,0,255);
  s.daily_task_done=raw.daily_task_done===1?1:0;
  // The petting budget arrived in v7. A v6 save has no budget, so it keeps the
  // fresh-state sentinel and the first pet of the day opens it.
  s.pet_xp_day=integer(raw.pet_xp_day,s.pet_xp_day,U32);
  s.pet_xp_today=integer(raw.pet_xp_today,0,255);
  // Placeable decor arrived in v8. An older save has no slots, so they keep the
  // fresh-state sentinels rather than inheriting whatever the blob contains --
  // kind 0 is a real item, so a zeroed slot would place twelve small rocks.
  s.decor_owned=integer(raw.decor_owned,0,U32);
  s.decor_owned_extra=integer(raw.decor_owned_extra,0,raw.version>=12?U32:3);
  if(raw.version>=12){
    s.backgrounds_owned_extra=raw.backgrounds_owned_extra??0;
    s.catalogue_owned_high=raw.catalogue_owned_high??0;
    s.borders_owned=raw.borders_owned??0;s.borders_owned_extra=raw.borders_owned_extra??0;
    s.border_id=raw.border_id??255;
  }
  if(raw.version>=10){
    s.music_enabled=raw.music_enabled??1;
    s.lights_dimmed=raw.lights_dimmed??0;
  }
  if(raw.version>=11){
    s.music_volume=raw.music_volume??100;
    s.music_track=raw.music_track??0;
  }
  if(raw.version>=15)s.usb_sounds=raw.usb_sounds;
  if(raw.version>=8&&Array.isArray(raw.decor))
    s.decor=Array.from({length:raw.decor.length},(_,i)=>{
      const item=raw.decor[i];
      if(!item||!Number.isInteger(item.kind)||item.kind===DECOR_EMPTY||!DECOR_KINDS[item.kind])
        return {kind:DECOR_EMPTY,blocking:0,x:0,y:0};
      return {kind:item.kind,blocking:item.blocking,
        x:round(clamp(item.x,34,446)),y:round(clamp(item.y,34,446))};
    });
  s.sick=raw.sick===1?1:0;s.muted=raw.muted===true||raw.muted===1?1:0;
  if(BACKGROUNDS.includes(raw.background??raw.theme))s.background=raw.background??raw.theme;
  s.backgrounds_owned=raw.version>=9?raw.backgrounds_owned:1|(1<<BACKGROUNDS.indexOf(s.background));
  for(const key of ['power_mode','screen_timeout','deep_sleep_after','rtc_wake'])
    if(Number.isInteger(raw[key]))s[key]=raw[key];
  if(typeof raw.sound==='boolean')s.muted=raw.sound?0:1;
  if(Number.isFinite(raw.brightness))s.brightness=clamp(Math.trunc(raw.brightness),10,100);
  if(Number.isFinite(raw.volume))s.volume=clamp(Math.trunc(raw.volume),0,100);
  if(Number.isFinite(raw.last_seen))s.last_seen=integer(raw.last_seen,0,U32);
  s.auto_feed=raw.version>=3&&(raw.auto_feed===1||raw.autoFeed===true)?1:0;
  s.auto_hunt=raw.version>=3&&(raw.auto_hunt===1||raw.autoHunt===true)?1:0;
  s.decor_locked=raw.version>=18&&raw.decor_locked===1?1:0;
  s.auto_clean=raw.version>=18&&raw.auto_clean===1?1:0;
  if(Array.isArray(raw.owned))for(const id of raw.owned)if(Number.isInteger(id)&&GIFTS[id])markGiftOwned(s,id);
  if(raw.gift&&typeof raw.gift==='object'&&raw.gift.used&&GIFTS[raw.gift.kind])
    s.gift={kind:raw.gift.kind,used:1,variant:integer(raw.gift.variant,0,255),
      x:clamp(raw.gift.x??315,34,446),y:clamp(raw.gift.y??330,34,446)};
  else if(Number.isInteger(raw.gift)&&GIFTS[raw.gift])s.gift={kind:raw.gift,used:1,variant:0,x:315,y:330};
  s.gift_path=integer(raw.gift_path,0,3);
  s.climb_owned=raw.version>=21?integer(raw.climb_owned,0,63):0;
  if(s.gift.used)markGiftOwned(s,s.gift.kind);
  // Restore the tank's mess. Older saves carry no dirt/bug tags, so a dropping
  // defaults to a spider dropping's worth.
  if(!(raw.version<3)&&Array.isArray(raw.poop))
    s.poop=raw.poop.slice(0,C.MAX_POOP).map(p=>({x:round(clamp(p.x,34,446)),y:round(clamp(p.y,34,446)),
      expires:p.expires,dirt:Number.isFinite(p.dirt)?p.dirt:C.POOP_DIRT_SPIDER,bug:p.bug===true}));
  s.gifts_owned>>>=0;s.achievements&=(1<<ACHIEVEMENTS.length)-1;
  s.level=Math.floor(s.xp/50)+1;
  s.days_lived=Math.floor(s.age_ticks/2880);
  s.stage=s.days_lived>=5?2:s.days_lived>=2?1:s.stage;
  s.spider_x=round(clamp(Number.isFinite(raw.spider_x)?raw.spider_x:Number.isFinite(raw.x)?raw.x:240,34,446));
  s.spider_y=round(clamp(Number.isFinite(raw.spider_y)?raw.spider_y:Number.isFinite(raw.y)?raw.y:240,34,446));
  s.spider_heading=Number.isFinite(raw.spider_heading)?wrap(raw.spider_heading):0;
  s._engine.px=s.spider_x;s._engine.py=s.spider_y;
  // Climb is no longer selectable, so a tank saved with Climb items would carry
  // a value the UI cannot express. Rewrite them to Walk Behind, the closest
  // surviving behaviour. Mirrors the decor_migrate_paths() call in save.c.
  migrateDecorPaths(s);
  syncView(s);
  return s;
}
// Hard ceiling on the Silk balance, mirroring SILK_MAX in main/game_config.h.
// Saturating at U32 let the balance reach hundreds of thousands, at which point
// every price is meaningless and a greyed shop row is indistinguishable from a
// Silk problem.
export const SILK_MAX = 99999;
export function addSilk(s,n){s.silk=Math.min(SILK_MAX,s.silk+integer(n));}
export function addXP(s,n){
  s.xp=(s.xp+integer(n))>>>0;
  const level=Math.floor(s.xp/50)+1;
  if(level!==s.level){s.level=level;if(level>=5)awardAchievement(s,'level_5');}
}
export function awardAchievement(s,id){
  const a=typeof id==='number'?ACHIEVEMENTS.find(a=>a.bit===id):ACHIEVEMENTS.find(a=>a.id===id);
  if(!a||s.achievements&a.bit)return false;
  companionEnsure(s);
  s.achievements|=a.bit;addXP(s,a.xp);addSilk(s,a.silk);companionEnsure(s);return true;
}
export function checkAchievements(s){
  const conditions={ten_cleans:s.cleans>=10,ten_plays:s.plays>=10,twenty_five_meals:s.meals>=25,
    bond_50:s.bond>=50,streak_3:s.daily_streak>=3,decor_3:decorPlacedCount(s)>=3,
    backgrounds_4:BACKGROUNDS.filter((_,id)=>backgroundOwned(s,id)).length>=4,
    first_border:BACKGROUNDS.some((_,id)=>borderOwned(s,id)),first_gift:!!s.gift.used};
  for(const [id,ready] of Object.entries(conditions))if(ready)awardAchievement(s,id);
}
// Generic native reward helpers, not achievement name dispatch.
export function award(s,xp,silk=0){addXP(s,xp);addSilk(s,silk);}
function event(s,type,details={}){s.lastEvent={type,at:runtime(s).now_ms,...details};return false;}
export function preyCount(s){return s.prey.filter(p=>p.alive&&!p.dead).length;}
export function speciesUnlocked(s,id){id=speciesId(id);return id>=0&&s.level>=SPECIES[id].unlock_level;}
export function nextSpecies(s){return SPECIES.find(p=>!speciesUnlocked(s,p.id))??null;}
function chooseSpecies(s){const a=SPECIES.filter(p=>speciesUnlocked(s,p.id));return a[roll(s,a.length)]?.id??0;}
export function spawnPrey(s,id=chooseSpecies(s),wild=false) {
  id=speciesId(id);
  if(!speciesUnlocked(s,id))return event(s,'rejected',{reason:'species locked'});
  let slot=s.prey.findIndex(p=>!p.alive);
  if(slot<0){if(s.prey.length>=3)return event(s,'rejected',{reason:'maximum 3 prey'});slot=s.prey.length;}
  const direction=random(s)*TAU-PI;
  const p={alive:true,dead:false,fleeing:false,wild,species:id,key:PREY[id],frame:0,
    frame_at:runtime(s).now_ms,gait_distance:0,retarget_at:0,dead_at:0,poop_at:runtime(s).now_ms+C.BUG_POOP_MS,
    graze_until:0,next_graze_at:runtime(s).now_ms+C.PREY_GRAZE_FIRST_MS,
    wander_dir:direction,heading:direction,x:0,y:0};
  for(let n=0;n<12;n++){p.x=34+roll(s,412);p.y=34+roll(s,412);if(Math.hypot(p.x-s.spider_x,p.y-s.spider_y)>90)break;}
  s.prey[slot]=p;return true;
}
export function feed(s,id){
  /* Feeding wakes a sleeping spider. Prey only becomes food when the spider
   * catches it, so dropping a bug in for a spider that is asleep did nothing
   * visible: the player paid a tap, the bug wandered, and the spider stayed
   * curled up. That is the "stuck asleep so I cannot feed them" report. */
  if(s.state===6){const r=runtime(s);r.sleep_forced=false;setState(s,0);}
  return spawnPrey(s,id??chooseSpecies(s),false);
}
/** A bug left in the tank leaves a small pale speckle rather than a dropping:
 * frass, not a meal's worth of waste, so it is worth less dirt. Same slot pool
 * and lifetime as a dropping. */
export function addBugPoop(s,x,y) {
  const r=runtime(s);
  const poop={x:round(clamp(x,34,446)),y:round(clamp(y,34,446)),
    expires:r.now_ms+C.POOP_LIFETIME_MS,dirt:C.POOP_DIRT_BUG,bug:true};
  if(s.poop.length>=C.MAX_POOP)s.poop.shift();
  s.poop.push(poop);
  return poop;
}
/** A spider dropping. Same slot pool and lifetime as a bug speckle, but worth a
 * full meal's dirt. */
export function addPoop(s,x,y) {
  const r=runtime(s);
  const poop={x:round(clamp(x,34,446)),y:round(clamp(y,34,446)),
    expires:r.now_ms+C.POOP_LIFETIME_MS,dirt:C.POOP_DIRT_SPIDER,bug:false};
  if(s.poop.length>=C.MAX_POOP)s.poop.shift();
  s.poop.push(poop);
  return poop;
}
/** Flies attracted by neglected waste.
 *
 * The device's terrarium.c gives a dropping that has been ignored for
 * FLY_ATTRACT_MS a fly that orbits it, and the fly goes when the dropping is
 * cleaned. This is the same cycle, derived from the poop list rather than kept
 * as separate state, so it cannot drift out of sync with the mess it describes.
 *
 * One fly per dropping, capped at MAX_FLIES, oldest dropping first. */
export const MAX_FLIES = 4;
export const FLY_ATTRACT_MS = 45000;

export function fliesAt(s) {
  const now = runtime(s).now_ms;
  const out = [];
  for (const poop of s.poop ?? []) {
    if (out.length >= MAX_FLIES) break;
    /* `expires` is stamped at creation, so the age is the lifetime minus what
     * is left. Deriving it this way means a restored save attracts flies at the
     * right time without storing a second timestamp. */
    const age = C.POOP_LIFETIME_MS - (poop.expires - now);
    if (age < FLY_ATTRACT_MS) continue;
    out.push({ x: poop.x, y: poop.y, age });
  }
  return out;
}
function setState(s,state) {
  if(s.state===state)return;
  s.state=state;
  const r=runtime(s);r.target=null;r.pounce=null;r.aim_active=false;r.drag=null;
  if(state===0)r.idle_until=r.now_ms+1200+roll(s,1800);
  if(state===8)r.oneshot_until=r.now_ms+800;
  if(state!==10)r.perch_heading=null;
  if(state!==10)r.perch_until=0;
}
/** Total dirt points in the tank. Clean pays in proportion to this, so the
 * reward tracks the mess actually removed rather than the tap. */
export function terrariumDirt(s) {
  let dirt=0;
  for(const p of s.poop??[])dirt+=p.dirt??C.POOP_DIRT_SPIDER;
  return dirt;
}
/** True when Clean would do something: droppings, live prey, or hygiene below
 * full. Informational only -- the button is never disabled. */
export function cleanNeeded(s) {
  return (s.poop?.length??0)>0||preyCount(s)>0||s.hygiene<C.CLEAN_HYGIENE_SET;
}
/** XP that cleaning would grant right now, without changing anything. The score
 * is the dirt swept up plus the hygiene restored, out of CLEAN_SCORE_FULL,
 * rounded up and capped at CLEAN_XP_MAX. Rounding up means any real mess pays at
 * least 1 XP; truncation would pay 0 for a single dropping. */
export function cleanReward(s) {
  let score=terrariumDirt(s);
  if(s.hygiene<C.CLEAN_HYGIENE_SET)score+=C.CLEAN_HYGIENE_SET-s.hygiene;
  if(score<=0)return 0;
  const xp=Math.ceil(score*C.CLEAN_XP_MAX/C.CLEAN_SCORE_FULL);
  return Math.min(C.CLEAN_XP_MAX,xp);
}
/** The in-game day the daily loop is on.
 *
 * The device prefers the RTC and falls back to the in-game counter. The browser
 * has a real clock, so it uses the calendar day directly: the check-in should
 * reset at midnight, not after 24 hours of play. `daily_override` wins over
 * both, which is how tests and the console drive the loop without waiting. */
export function dailyToday(s){
  if(s.daily_override!==undefined&&s.daily_override!==C.DAILY_NEVER)return s.daily_override;
  const now=Date.now();
  if(Number.isFinite(now)&&now>0)return Math.floor(now/86400000);
  return Math.floor(s.age_ticks/C.TICKS_PER_DAY);
}
/** The rotating task for a day. Deterministic, so a reload cannot reroll it. */
export function dailyTaskForDay(day){return day%C.DAILY_TASK_COUNT;}
export function dailyTask(s){return dailyTaskForDay(dailyToday(s));}
const DAILY_COUNTERS=['meals','cleans','plays','pets'];
export function dailyTaskGoal(s){
  switch(dailyTask(s)){case 0:return 3;case 1:return 2;case 2:return 3;default:return 4;}
}
export function dailyTaskName(task){
  switch(task){case 0:return 'Catch 3 Prey';case 1:return 'Clean 2 Times';
    case 2:return 'Play 3 Times';default:return 'Pet 4 Times';}
}
/** Snapshot the task baseline when the day turns over. Taking it lazily would
 * capture the baseline AFTER the player acted, so progress would read 0. */
export function dailyEnsureBase(s){
  const today=dailyToday(s);
  if(s.daily_base_day===today)return;
  s.daily_base_day=today;
  s.daily_task_base=s[DAILY_COUNTERS[dailyTaskForDay(today)]]??0;
  s.daily_task_done=0;
}
export function dailyTaskProgress(s){
  const now=s[DAILY_COUNTERS[dailyTask(s)]]??0;
  const base=s.daily_task_base??0;
  if(now<base)return 0;
  return Math.min(now-base,dailyTaskGoal(s));
}
export function dailyTaskReady(s){
  return !s.daily_task_done&&dailyTaskProgress(s)>=dailyTaskGoal(s);
}
/** A streak of 0 means "never claimed", which is self-healing: a save whose
 * daily_day happens to equal today but has no streak is treated as unclaimed. */
export function dailyAvailable(s){
  if(!s.daily_streak)return true;
  return s.daily_day!==dailyToday(s);
}
/** Claim today's check-in. Returns the XP paid, or 0 if already claimed. */
export function dailyClaim(s){
  const today=dailyToday(s);
  if(!dailyAvailable(s))return 0;
  if(s.daily_day!==C.DAILY_NEVER&&s.daily_day+1===today&&s.daily_streak>0){
    if(s.daily_streak<C.DAILY_STREAK_MAX)s.daily_streak++;
  }else s.daily_streak=1;
  const step=s.daily_streak-1;
  const xp=C.DAILY_XP_BASE+step*C.DAILY_XP_PER_DAY;
  const silk=C.DAILY_SILK_BASE+step*C.DAILY_SILK_PER_DAY;
  s.daily_day=today;
  addXP(s,xp);addSilk(s,silk);
  return xp;
}
/** Pay today's task if complete. Returns the XP paid, or 0 if not ready. */
export function dailyTaskClaim(s){
  if(!dailyTaskReady(s))return 0;
  s.daily_task_done=1;
  addXP(s,C.DAILY_TASK_XP);addSilk(s,C.DAILY_TASK_SILK);
  return C.DAILY_TASK_XP;
}
export const TREATS=Object.freeze([
  {name:'Juicy Fly',asset:'treat_hunger',cost:C.TREAT_HUNGER_COST,field:'hunger',gain:C.TREAT_HUNGER_GAIN},
  {name:'Silk Ribbon',asset:'treat_mood',cost:C.TREAT_MOOD_COST,field:'mood',gain:C.TREAT_MOOD_GAIN},
  {name:'Warm Pebble',asset:'treat_energy',cost:C.TREAT_ENERGY_COST,field:'energy',gain:C.TREAT_ENERGY_GAIN},
  {name:'Honey Moth',asset:'treat_dew',cost:14,field:'hunger',gain:30,secondary:'mood',bonus:12},
  {name:'Dew Drop',asset:'treat_pollen',cost:12,field:'hygiene',gain:25,secondary:'energy',bonus:12},
  {name:'Cozy Nest',asset:'treat_silk',cost:18,field:'energy',gain:28,secondary:'mood',bonus:20},
  {name:'Moon Mousse',asset:'treat_moon',cost:16,field:'mood',gain:28,secondary:'energy',bonus:14},
  {name:'Amber Drop',asset:'treat_amber',cost:14,field:'hunger',gain:32,secondary:'energy',bonus:8},
  {name:'Neon Jelly',asset:'treat_neon',cost:18,field:'energy',gain:30,secondary:'mood',bonus:16},
  {name:'Star Wafer',asset:'treat_star',cost:20,field:'hunger',gain:28,secondary:'energy',bonus:22},
  {name:'Lotus Nectar',asset:'treat_lotus',cost:16,field:'hygiene',gain:30,secondary:'mood',bonus:12},
  {name:'Rune Biscuit',asset:'treat_rune',cost:22,field:'hunger',gain:20,secondary:'mood',bonus:24},
]);
/** Buy and apply a treat. Returns false when Silk is short. */
export function buyTreat(s,kind){
  const t=TREATS[kind];
  if(!t)return false;
  if(s.silk<t.cost)return event(s,'rejected',{reason:'not enough Silk'});
  companionEnsure(s);
  const positive=s[t.field]<100||(t.secondary&&s[t.secondary]<100);
  s.silk-=t.cost;
  s[t.field]=clamp(s[t.field]+t.gain);
  if(t.secondary)s[t.secondary]=clamp(s[t.secondary]+t.bonus);
  if(positive)companionNote(s,5,kind);
  return true;
}
/** Placeable decor. Backgrounds are ground texture only; everything that sits
 * on the floor is bought with Silk and placed by the player. Default path0
 * is Walk Over, path1 Solid; players can also select path2 Walk Behind.
 * Order must match decor_kind_t in main/decor.h. */
export const DECOR_KINDS=Object.freeze([
  {key:'rock_small',name:'Small Rock',cost:18,level:1,radius:9,blocking:0,climb:6,artScale:115},
  {key:'rock_large',name:'Large Rock',cost:34,level:2,radius:16,blocking:1,climb:14,artScale:100},
  {key:'pebbles',name:'Pebbles',cost:14,level:1,radius:8,blocking:0,climb:3,artScale:85},
  {key:'log',name:'Log',cost:46,level:4,radius:22,blocking:1,climb:16,artScale:105},
  {key:'bark',name:'Bark Slab',cost:22,level:3,radius:14,blocking:0,climb:7,artScale:90},
  {key:'fern',name:'Fern',cost:38,level:5,radius:15,blocking:0,climb:0,artScale:135},
  {key:'moss_clump',name:'Moss Clump',cost:20,level:2,radius:11,blocking:0,climb:4,artScale:80},
  {key:'succulent',name:'Succulent',cost:52,level:7,radius:14,blocking:1,climb:0,artScale:110},
  {key:'flower_white',name:'White Flower',cost:26,level:3,radius:8,blocking:0,climb:0,artScale:75},
  {key:'flower_pink',name:'Pink Flower',cost:30,level:6,radius:8,blocking:0,climb:0,artScale:75},
  {key:'mushroom',name:'Mushroom',cost:28,level:4,radius:10,blocking:0,climb:5,artScale:90},
  {key:'shell',name:'Shell',cost:24,level:5,radius:9,blocking:0,climb:4,artScale:80},
  {key:'crystal',name:'Crystal',cost:68,level:9,radius:12,blocking:1,climb:12,artScale:120},
  {key:'leaf_pile',name:'Leaf Pile',cost:16,level:1,radius:13,blocking:0,climb:3,artScale:95},
  {key:'twig',name:'Twig',cost:12,level:1,radius:12,blocking:0,climb:3,artScale:70},
  {key:'bone',name:'Bone',cost:58,level:8,radius:13,blocking:1,climb:6,artScale:105},
  {key:'fern_pot',name:'Fern Planter',cost:48,level:6,radius:14,blocking:1,climb:0,artScale:125},
  {key:'acorn_hide',name:'Acorn Hide',cost:72,level:10,radius:15,blocking:1,climb:10,artScale:95},
  {key:'driftwood_arch',name:'Driftwood Arch',cost:82,level:12,radius:18,blocking:1,climb:18,artScale:115},
  {key:'web_hammock',name:'Web Hammock',cost:64,level:8,radius:15,blocking:0,climb:8,artScale:110},
  {key:'dew_pool',name:'Dew Pool',cost:54,level:7,radius:14,blocking:0,climb:0,artScale:90},
  {key:'mushroom_pair',name:'Mushroom Pair',cost:62,level:8,radius:13,blocking:0,climb:6,artScale:100},
  {key:'flower_planter',name:'Flower Planter',cost:68,level:9,radius:15,blocking:1,climb:0,artScale:120},
  {key:'crystal_cluster',name:'Crystal Cluster',cost:98,level:14,radius:15,blocking:1,climb:16,artScale:130},
  {key:'shell_cove',name:'Shell Cove',cost:74,level:11,radius:16,blocking:1,climb:8,artScale:105},
  {key:'twig_ladder',name:'Twig Ladder',cost:46,level:6,radius:13,blocking:0,climb:12,artScale:100},
  {key:'moss_stone',name:'Mossy Stone',cost:42,level:5,radius:13,blocking:0,climb:8,artScale:95},
  {key:'seed_pod',name:'Seed Pod',cost:38,level:4,radius:10,blocking:0,climb:4,artScale:75},
  {key:'silk_tent',name:'Silk Tent',cost:112,level:16,radius:19,blocking:1,climb:14,artScale:125},
  {key:'leaf_umbrella',name:'Leaf Umbrella',cost:88,level:13,radius:15,blocking:1,climb:0,artScale:130},
  {key:'bone_arch',name:'Bone Arch',cost:96,level:15,radius:16,blocking:1,climb:16,artScale:115},
  {key:'fossil',name:'Fossil',cost:58,level:8,radius:12,blocking:0,climb:5,artScale:90},
  {key:'lantern_cap',name:'Lantern Cap',cost:92,level:14,radius:13,blocking:0,climb:9,artScale:105},
  {key:'stone_bridge',name:'Stone Bridge',cost:120,level:18,radius:20,blocking:1,climb:18,artScale:120},
  {key:'fairy_ring',name:'Fairy Ring',cost:44,level:4,radius:17,blocking:0,climb:0,artScale:110},
  {key:'foxglove_lantern',name:'Foxglove Lantern',cost:58,level:6,radius:12,blocking:1,climb:0,artScale:95},
  {key:'pixie_door',name:'Pixie Door',cost:70,level:8,radius:14,blocking:1,climb:0,artScale:90},
  {key:'gargoyle',name:'Gargoyle',cost:78,level:9,radius:15,blocking:1,climb:14,artScale:110},
  {key:'iron_gate',name:'Iron Gate',cost:84,level:10,radius:19,blocking:1,climb:18,artScale:120},
  {key:'rose_urn',name:'Rose Urn',cost:54,level:6,radius:12,blocking:1,climb:10,artScale:95},
  {key:'book_stack',name:'Book Stack',cost:40,level:3,radius:12,blocking:1,climb:8,artScale:100},
  {key:'globe',name:'Globe',cost:66,level:7,radius:13,blocking:1,climb:0,artScale:90},
  {key:'quill_desk',name:'Quill Desk',cost:90,level:11,radius:19,blocking:1,climb:14,artScale:115},
  {key:'tea_cup',name:'Tea Cup',cost:32,level:2,radius:13,blocking:1,climb:7,artScale:85},
  {key:'spool',name:'Spool',cost:26,level:2,radius:10,blocking:1,climb:6,artScale:75},
  {key:'thimble',name:'Thimble',cost:30,level:3,radius:10,blocking:1,climb:6,artScale:70},
  {key:'cauldron',name:'Cauldron',cost:74,level:8,radius:17,blocking:1,climb:12,artScale:110},
  {key:'broom',name:'Broom',cost:38,level:4,radius:15,blocking:0,climb:3,artScale:105},
  {key:'potion_rack',name:'Potion Rack',cost:86,level:10,radius:17,blocking:1,climb:14,artScale:115},
  {key:'amber_fossil',name:'Amber Fossil',cost:58,level:6,radius:12,blocking:0,climb:5,artScale:90},
  {key:'cycad',name:'Cycad',cost:62,level:7,radius:16,blocking:1,climb:0,artScale:130},
  {key:'dinosaur_egg',name:'Dinosaur Egg',cost:72,level:9,radius:13,blocking:1,climb:9,artScale:100},
  {key:'neon_sign',name:'Neon Sign',cost:80,level:9,radius:14,blocking:1,climb:0,artScale:105},
  {key:'holo_terminal',name:'Holo Terminal',cost:96,level:12,radius:15,blocking:1,climb:12,artScale:110},
  {key:'cable_coil',name:'Cable Coil',cost:42,level:4,radius:12,blocking:0,climb:4,artScale:85},
  {key:'moon_rover',name:'Moon Rover',cost:104,level:13,radius:18,blocking:1,climb:12,artScale:115},
  {key:'solar_array',name:'Solar Array',cost:88,level:10,radius:18,blocking:1,climb:10,artScale:120},
  {key:'oxygen_pod',name:'Oxygen Pod',cost:92,level:11,radius:14,blocking:1,climb:11,artScale:100},
  {key:'wizard_tower',name:'Wizard Tower',cost:112,level:14,radius:17,blocking:1,climb:16,artScale:125},
  {key:'dragon_statue',name:'Dragon Statue',cost:120,level:16,radius:18,blocking:1,climb:16,artScale:120},
  {key:'treasure_chest',name:'Treasure Chest',cost:98,level:12,radius:15,blocking:1,climb:11,artScale:105},
  {key:'sun_obelisk',name:'Sun Obelisk',cost:108,level:13,radius:13,blocking:1,climb:13,artScale:115},
  {key:'glyph_tablet',name:'Glyph Tablet',cost:64,level:7,radius:14,blocking:0,climb:4,artScale:95},
  {key:'broken_column',name:'Broken Column',cost:76,level:8,radius:15,blocking:1,climb:12,artScale:110},
  {key:'stone_lantern',name:'Stone Lantern',cost:70,level:6,radius:13,blocking:1,climb:11,artScale:100},
  {key:'bamboo_fountain',name:'Bamboo Fountain',cost:84,level:9,radius:16,blocking:1,climb:14,artScale:115},
  {key:'bonsai',name:'Bonsai',cost:94,level:11,radius:16,blocking:1,climb:0,artScale:125},
  {key:'fairy_bridge',name:'Fairy Bridge',cost:102,level:12,radius:20,blocking:1,climb:16,artScale:120},
  {key:'climb_branch',name:'Fallen Branch',cost:140,level:15,radius:60,blocking:1,climb:46,big:1},
  {key:'climb_vine',name:'Vine Arch',cost:160,level:17,radius:64,blocking:1,climb:54,big:1},
  {key:'climb_root',name:'Root Bridge',cost:150,level:16,radius:62,blocking:1,climb:50,big:1},
  {key:'climb_ivy',name:'Ivy Column',cost:132,level:14,radius:52,blocking:1,climb:58,big:1},
  {key:'climb_boulder',name:'Mossy Boulder',cost:124,level:13,radius:56,blocking:1,climb:40,big:1},
  {key:'climb_driftwood',name:'Driftwood Ramp',cost:170,level:18,radius:66,blocking:1,climb:62,big:1},
]);
/* First kind in the large climb-structure family. These are authored at
 * 192x192 and drawn 1:1, and their lift ramps from the rim to the centre. */
export const DECOR_CLIMB_FIRST=68;
// Authored at 96 and drawn at LVGL scale 512 (2x), so the climb structures
// share the same 2-screen-pixels-per-source-pixel density as the gifts and
// props. Mirrors DECOR_CLIMB_SPRITE in main/decor.h.
export const DECOR_CLIMB_SPRITE=96;
// Screen half-extent of a large climb structure's footprint. The sprite is
// DECOR_CLIMB_SPRITE square drawn at 2x, so its half-extent is DECOR_CLIMB_SPRITE.
// `radius` is the much smaller collision footprint and must not be used for the
// climb ramp. Mirrors DECOR_CLIMB_EXTENT in main/decor.h.
export const DECOR_CLIMB_EXTENT=DECOR_CLIMB_SPRITE;
// Half-extent used for the climb ramp: the artwork for a large structure, the
// collision footprint for a small prop.
export function decorClimbExtent(kind){
  const d=DECOR_KINDS[kind];
  if(!d)return 0;
  return decorIsClimbStructure(kind)?DECOR_CLIMB_EXTENT:d.radius;
}
export function decorIsClimbStructure(kind){
  return kind>=DECOR_CLIMB_FIRST&&kind<DECOR_KINDS.length;
}
export const DECOR_SLOTS=12;
export const DECOR_EMPTY=0xFF;
export function decorSprite(kind){
  const d=DECOR_KINDS[kind];
  return d?`decoritem_${d.key}`:null;
}
export function decorUnlocked(s,kind){
  const d=DECOR_KINDS[kind];
  return !!d&&s.level>=d.level;
}
export function decorOwned(s,kind){
  if(!(kind>=0&&kind<DECOR_KINDS.length))return false;
  if(kind<64)return ((kind<32?s.decor_owned:s.decor_owned_extra)&(1<<(kind%32)))!==0;
  if(kind<DECOR_CLIMB_FIRST)return (s.catalogue_owned_high&(1<<(kind-64+4)))!==0;
  return ((s.climb_owned??0)&(1<<(kind-DECOR_CLIMB_FIRST)))!==0;
}
/** Buy one decor kind. Returns false and records a rejection when refused. */
export function buyDecor(s,kind){
  const d=DECOR_KINDS[kind];
  if(!d)return event(s,'rejected',{reason:'unknown decor'});
  if(!decorUnlocked(s,kind))return event(s,'rejected',{reason:'decor locked'});
  if(s.silk<d.cost)return event(s,'rejected',{reason:'not enough Silk'});
  s.silk-=d.cost;
  if(kind<64){
    const field=kind<32?'decor_owned':'decor_owned_extra';
    s[field]=(s[field]|(1<<(kind%32)))>>>0;
  }else if(kind<DECOR_CLIMB_FIRST){
    s.catalogue_owned_high=(s.catalogue_owned_high|(1<<(kind-64+4)))>>>0;
  }else{
    s.climb_owned=((s.climb_owned??0)|(1<<(kind-DECOR_CLIMB_FIRST)))>>>0;
  }
  return true;
}
export function decorPlacedCount(s){
  return s.decor.filter(d=>d.kind!==DECOR_EMPTY).length;
}
/** Place an owned kind. Returns the slot index, or -1 when refused. */
export function placeDecor(s,kind,x,y){
  const d=DECOR_KINDS[kind];
  if(!d)return -1;
  if(!decorOwned(s,kind))return -1;
  if(s.decor_locked)return -1;
  if(preyCount(s)>0)return -1;
  let slot=s.decor.findIndex(item=>item.kind===DECOR_EMPTY);
  if(slot<0)slot=s.decor.length;
  const r=d.radius;
  s.decor[slot]={kind,blocking:d.blocking,
    x:round(clamp(x,34+r,446-r)),y:round(clamp(y,34+r,446-r))};
  return slot;
}
/** Move a placed item. Keeps its slot, kind and blocking flag; only the
 * position changes, clamped inside the playfield. */
export function moveDecor(s,slot,x,y){
  if(slot<0||slot>=s.decor.length||s.decor[slot].kind===DECOR_EMPTY)return false;
  if(s.decor_locked)return false;
  if(preyCount(s)>0)return false;
  const d=DECOR_KINDS[s.decor[slot].kind];
  if(!d)return false;
  const r=d.radius;
  s.decor[slot].x=round(clamp(x,34+r,446-r));
  s.decor[slot].y=round(clamp(y,34+r,446-r));
  return true;
}
export function removeDecor(s,slot){
  if(slot<0||slot>=s.decor.length||s.decor[slot].kind===DECOR_EMPTY)return false;
  s.decor[slot]={kind:DECOR_EMPTY,blocking:0,x:0,y:0};
  return true;
}
// Climb is retired: the 2.5D lift never read convincingly on the panel, so the
// option was removed rather than tuned further. Path 3 survives in the enum only
// so old saves parse; migrateDecorPaths() rewrites it to Walk Behind on load.
export const DECOR_PATH_NAMES=['Walk Over','Solid','Walk Behind'];
export const DECOR_SELECTABLE_PATHS=3;
export function toggleDecorBlocking(s,slot){
  if(slot<0||slot>=s.decor.length||s.decor[slot].kind===DECOR_EMPTY)return false;
  // Three selectable paths. Climb was removed: the 2.5D lift never read
  // convincingly, so the large structures are ordinary Walk Behind items.
  let path=s.decor[slot].blocking;
  if(path>=DECOR_SELECTABLE_PATHS)path=0;
  s.decor[slot].blocking=(path+1)%DECOR_SELECTABLE_PATHS;
  return true;
}
/** Rewrite legacy Climb items to Walk Behind. Mirrors decor_migrate_paths(). */
export function migrateDecorPaths(s){
  let migrated=0;
  for(const item of s.decor??[]){
    if(item.kind===DECOR_EMPTY||item.blocking!==3)continue;
    item.blocking=2;migrated++;
  }
  return migrated;
}
export const decorCanopy=path=>path===2;
export const decorClimbable=kind=>(DECOR_KINDS[kind]?.climb??0)>0;
export const decorClimbHeight=(s,slot)=>{
  const item=s.decor[slot];
  if(!item||item.kind===DECOR_EMPTY||item.blocking!==3)return 0;
  return DECOR_KINDS[item.kind]?.climb??0;
};
// The opaque span of a large structure's artwork at a screen point, or null
// when the point is off the artwork. The sprites are diagonal logs and arches,
// so a circle centred on the sprite covers a lot of empty corner: the spider
// was lifted while standing in transparent space beside the log, with the
// contact shadow left far away at the floor point.
export function climbSpanAt(kind,x,y,itemX,itemY){
  const index=kind-DECOR_CLIMB_FIRST;
  if(index<0||index>=CLIMB_MASK_COUNT)return null;
  // LVGL scales an image about its own centre, so the sprite covers
  // item +/- DECOR_CLIMB_EXTENT and each source pixel covers two screen pixels.
  const sx=Math.floor((x-itemX)/2)+DECOR_CLIMB_SPRITE/2;
  const sy=Math.floor((y-itemY)/2)+DECOR_CLIMB_SPRITE/2;
  if(sx<0||sx>=CLIMB_MASK_ROWS||sy<0||sy>=CLIMB_MASK_ROWS)return null;
  const span=CLIMB_MASK[index][sy];
  if(!span)return null;
  if(sx<span[0]||sx>span[1])return null;
  return {span,sx,sy};
}
// The first and last opaque source row of a structure, so the ramp is measured
// against the artwork rather than the square box around it.
export function climbRowRange(kind){
  const index=kind-DECOR_CLIMB_FIRST;
  if(index<0||index>=CLIMB_MASK_COUNT)return null;
  const rows=CLIMB_MASK[index];
  let top=0,bot=rows.length-1;
  while(top<rows.length&&!rows[top])top++;
  while(bot>top&&!rows[bot])bot--;
  return {top,bot};
}
export function decorClimbAt(s,x,y){
  let best=0;
  for(const item of s.decor){
    if(item.kind===DECOR_EMPTY||item.blocking!==3)continue;
    const def=DECOR_KINDS[item.kind];
    if(!def||!def.climb)continue;
    const dx=x-item.x,dy=y-item.y;
    const r=decorClimbExtent(item.kind);
    if(r<=0)continue;
    // A prop is a flat disc: inside the footprint you are at full height.
    if(!decorIsClimbStructure(item.kind)){
      if(dx*dx+dy*dy<=r*r&&def.climb>best)best=def.climb;
      continue;
    }
    // A large structure is a SLOPE that runs UP THE SCREEN: the spider enters at
    // the bottom of the artwork and rises as it walks toward the top, which is
    // what makes it read as climbing a branch rather than floating over one.
    //
    // There is deliberately no circular pre-check: the sprite is a square box,
    // so its corners sit at r*sqrt(2) from the centre and a circle of radius r
    // clipped the ends of the diagonal logs. The span table is exact.
    const hit=climbSpanAt(item.kind,x,y,item.x,item.y);
    if(!hit)continue;
    const range=climbRowRange(item.kind);
    const t=Math.min(1,Math.max(0,(hit.sy-range.top)/(range.bot-range.top+1)));
    // Fade out toward the left and right edges of the artwork, measured against
    // the row's own span so the fade follows the diagonal, not the square.
    const mid=(hit.span[0]+hit.span[1])/2;
    const half=(hit.span[1]-hit.span[0])/2+1;
    const nx=(hit.sx-mid)/half;
    const fade=Math.max(0,1-nx*nx);
    const lift=Math.round(def.climb*(1-t)*fade);
    if(lift>best)best=lift;
  }
  return best;
}

// The surface slope under a point, as a signed -1..1 gradient. Mirrors
// decor_climb_slope() in main/decor.c.
//
// A large climb structure ramps linearly from ground level at its bottom edge
// to full height at its top edge, so its surface has a constant gradient of
// climb_height / (2 * radius). The renderer uses this to tilt the spider with
// the ramp; without it the creature stays level while the ground slides past,
// which is what made the climb read as a sprite moving up the screen.
//
// The sign follows screen y, where y grows downward: a surface rising toward
// the top of the screen is NEGATIVE. A small prop is a flat disc, so its top
// is level and the slope is 0.
export function decorClimbSlope(s,x,y){
  let best=0,bestLift=0;
  for(const item of s.decor){
    if(item.kind===DECOR_EMPTY||item.blocking!==3)continue;
    const def=DECOR_KINDS[item.kind];
    if(!def||!def.climb)continue;
    const lift=decorClimbAt({decor:[item]},x,y);
    if(lift===0||lift<=bestLift)continue;
    bestLift=lift;
    if(!decorIsClimbStructure(item.kind)){best=0;continue;}
    // The run is the artwork's own vertical span, matching the ramp in
    // decorClimbAt(): the driftwood's opaque rows are 10..87 of 96, so using
    // the square box would understate the gradient. Source rows are 2 screen
    // pixels each.
    const range=climbRowRange(item.kind);
    const run=(range.bot-range.top+1)*2;
    if(run<=0){best=0;continue;}
    best=-def.climb/run;
  }
  return best;
}

// Project a body-local (forward, right) offset into canvas coordinates.
//
// The spider is always drawn from directly above: there is no dorsal axis, no
// profile blend and no body tilt. See the note near the top of this file for why
// the climb perspective was removed rather than tuned. Mirrors to_canvas().
export function toCanvas(fwd,right,heading,scaleF,scaleR,foreshorten){
  const fx=Math.cos(heading),fy=Math.sin(heading);
  const rx=-fy,ry=fx;
  const f=fwd*scaleF*foreshorten;
  const r=right*scaleR;
  return {x:fx*f+rx*r,y:fy*f+ry*r};
}

// Perching on a small prop. A prop is too short to read as a climb, so the
// spider stops on top and turns to face out from the item; the pose sells the
// height, not the lift. Large structures are walked across instead.
// Minimum perch footprint, in pixels. A prop's `radius` is its collision
// footprint, which for small items is SMALLER than the spider's own body: the
// Book Stack is radius 12 while the body spans about 26 px. Using the raw
// radius meant the spider's centre had to land within 12 px of the item's
// centre to perch, a window it crossed in a fraction of a tick while
// wandering, so it appeared to jump straight over the books. Mirrors
// PERCH_MIN_RADIUS in main/decor.c.
export const PERCH_MIN_RADIUS = 20;
export function decorPerchItem(s,x,y){
  let best=null,bestLift=0;
  for(const item of s.decor){
    if(item.kind===DECOR_EMPTY||item.blocking!==3)continue;
    if(decorIsClimbStructure(item.kind))continue;
    const def=DECOR_KINDS[item.kind];
    if(!def||!def.climb)continue;
    const r=Math.max(def.radius,PERCH_MIN_RADIUS);
    const dx=x-item.x,dy=y-item.y;
    if(dx*dx+dy*dy>r*r)continue;
    if(def.climb>bestLift){bestLift=def.climb;best=item;}
  }
  return best;
}
export const decorPerchAt=(s,x,y)=>decorPerchItem(s,x,y)!==null;
// The heading that reads as "sitting on top, looking out": straight out from
// the item's centre, snapped to the eight compass directions the artwork is
// drawn for so the leg fan lands on a stable angle.
export function decorPerchHeading(s,x,y){
  const item=decorPerchItem(s,x,y);
  if(!item)return null;
  const dx=x-item.x,dy=y-item.y;
  const h=Math.hypot(dx,dy)<1.5?Math.PI/2:Math.atan2(dy,dx);
  const step=Math.PI/4;
  return Math.round(h/step)*step;
}
// The centre of the small prop under a point, which is where a perched spider
// holds position. Derived from the item rather than cached, so a forced state
// change or a save load that restores PERCH still converges on the real prop.
export function decorPerchCenter(s,x,y){
  const item=decorPerchItem(s,x,y);
  return item?{x:item.x,y:item.y}:null;
}
/** Half-extent of an item's ARTWORK on screen, in pixels.
 *
 * The same number the renderer uses to size the sprite, so it is the honest
 * measure of how much of the tank an item covers. A 64x64 source drawn at scale
 * radius*16 covers 4*radius screen pixels, so its half-extent is 2*radius,
 * multiplied by the kind's art_scale. Mirrors item_extent() in main/decor.c. */
export function decorExtent(kind){
  const d=DECOR_KINDS[kind];
  if(!d)return 0;
  if(d.big)return DECOR_CLIMB_SPRITE;
  return d.radius*2*(d.artScale??100)/100;
}
/** True when two items' artwork actually overlaps on screen.
 *
 * Draw order is only visible where two sprites cover the same pixels, so
 * ordering a fern against a rock at the far end of the tank changes nothing the
 * player can see. Mirrors items_overlap() in main/decor.c. */
export function decorOverlaps(a,b){
  const dx=a.x-b.x,dy=a.y-b.y,reach=decorExtent(a.kind)+decorExtent(b.kind);
  return dx*dx+dy*dy<reach*reach;
}
/** Slots in the same band that OVERLAP the reference item.
 *
 * `ref` is {kind,x,y} so a not-yet-placed item can be measured too. */
export function decorLayerSlots(s,path,exclude=-1,ref=null){
  const base=ref??(exclude>=0&&s.decor[exclude]?.kind!==DECOR_EMPTY?s.decor[exclude]:null);
  return s.decor.flatMap((item,slot)=>{
    if(slot===exclude||item.kind===DECOR_EMPTY)return [];
    if(decorCanopy(item.blocking)!==decorCanopy(path))return [];
    if(base&&!decorOverlaps(base,item))return [];
    return [slot];
  });
}
/** The overlapping items in a slot's band, INCLUDING the slot itself, in draw
 * order (which is array order). This is the list the layer index and the
 * reorder operate on: excluding the item would make indexOf() return -1 and the
 * reorder a no-op, and putting it first would pin its index at 0. */
export function decorLayerOrder(s,slot){
  const item=s.decor[slot];
  if(!item||item.kind===DECOR_EMPTY)return [];
  const others=decorLayerSlots(s,item.blocking,slot);
  return [...others,slot].sort((a,b)=>a-b);
}
export function decorLayerIndex(s,slot){
  return decorLayerOrder(s,slot).indexOf(slot);
}
// The gift's layer. The gift is a single item, so it has no siblings to order
// against; its layer is its position among the DECOR it actually OVERLAPS,
// which is what the player is arranging. The count includes the gift itself.
// Mirrors decor_gift_layer_count/index/set_layer in main/decor.c.
const GIFT_EXTENT=64;
export function giftLayerSlots(s,path){
  if(!s.gift?.used)return [];
  const ref={kind:-1,x:s.gift.x,y:s.gift.y};
  return s.decor.flatMap((item,slot)=>{
    if(item.kind===DECOR_EMPTY)return [];
    if(decorCanopy(item.blocking)!==decorCanopy(path))return [];
    const dx=ref.x-item.x,dy=ref.y-item.y,reach=GIFT_EXTENT+decorExtent(item.kind);
    return dx*dx+dy*dy<reach*reach?[slot]:[];
  });
}
export function giftLayerCount(s,path){
  return giftLayerSlots(s,path).length+1;
}
export function giftLayerIndex(s,path){
  const count=giftLayerSlots(s,path).length;
  return Math.min(Math.max(0,s.gift_layer??0),count);
}
export function setGiftLayer(s,path,index){
  const count=giftLayerSlots(s,path).length;
  if(!Number.isInteger(index)||index<0||index>count)
    return event(s,'rejected',{reason:'invalid gift layer'});
  s.gift_layer=index;
  return true;
}
export function setDecorLayer(s,slot,index){
  const item=s.decor[slot];
  if(!item||item.kind===DECOR_EMPTY)return event(s,'rejected',{reason:'missing decor'});
  const slots=decorLayerOrder(s,slot);
  if(!Number.isInteger(index)||index<0||index>=slots.length)
    return event(s,'rejected',{reason:'invalid decor layer'});
  const items=slots.map(i=>s.decor[i]),from=slots.indexOf(slot);
  items.splice(index,0,items.splice(from,1)[0]);
  slots.forEach((value,i)=>{s.decor[value]=items[i];});
  return true;
}
/** The placed item whose footprint contains a point, or -1. */
export function decorHitTest(s,x,y){
  for(const canopy of [true,false])
  for(let i=s.decor.length-1;i>=0;i--){
    const item=s.decor[i];
    if(item.kind===DECOR_EMPTY||decorCanopy(item.blocking)!==canopy)continue;
    const d=DECOR_KINDS[item.kind];
    if(!d)continue;
    const dx=x-item.x,dy=y-item.y;
    const hit=Math.max(28,d.radius);
    if(dx*dx+dy*dy<=hit*hit)return i;
  }
  return -1;
}
// Walk Behind blocks movement as well as occluding. It used to be
// non-blocking, which let the spider walk over the bottom of the sprite and
// then pop behind the top. Mirrors decor_path_blocks() in main/decor.c.
export function decorPathBlocks(path){
  return path===1||path===2;
}
// The radius an item actually blocks at. Solid blocks its collision footprint;
// Walk Behind blocks the whole ARTWORK, because the point of that path is that
// the spider must not stand on the item's lower half. A prop's sprite is 64x64
// drawn at radius*16, so it covers 2*radius screen pixels each way; a large
// climb structure is DECOR_CLIMB_SPRITE square drawn at 2x.
export function decorBlockRadius(s,slot){
  const item=s.decor[slot];
  if(!item||item.kind===DECOR_EMPTY)return 0;
  const d=DECOR_KINDS[item.kind];
  if(!d)return 0;
  // Both blocking paths block against the artwork, so the berth the steering
  // uses has to be the same number decorBlocksPoint() tests.
  return decorArtExtent(item.kind);
}
/** Half-extent of an item's ARTWORK on screen, in pixels.
 *
 * This is the same number the renderer uses to size the sprite, so it is the
 * honest measure of how much of the tank an item covers. A 64x64 source drawn
 * at scale radius*16 covers 4*radius screen pixels, so its half-extent is
 * 2*radius. art_scale then scales the sprite without touching the footprint, so
 * it has to be applied here or a scaled-up prop blocks less than it covers. */
export function decorArtExtent(kind){
  const d=DECOR_KINDS[kind];
  if(!d)return 0;
  if(decorIsClimbStructure(kind))return DECOR_CLIMB_EXTENT;
  return d.radius*2*(d.artScale??100)/100;
}
/** True when a point is inside a blocking item's footprint. */
export function decorBlocksPoint(s,x,y){
  for(const item of s.decor){
    if(item.kind===DECOR_EMPTY||!decorPathBlocks(item.blocking))continue;
    const d=DECOR_KINDS[item.kind];
    if(!d)continue;
    const dx=x-item.x,dy=y-item.y;
    // A large structure's artwork is a diagonal log, so the square box around
    // it is mostly empty. Blocking the box would make the spider refuse to walk
    // through thin air beside the log. The span table is exact, so it replaces
    // the circular test entirely: the box corners sit at r*sqrt(2), and a
    // circle of radius r clipped the ends of the logs.
    if(item.blocking===2&&decorIsClimbStructure(item.kind)){
      if(!climbSpanAt(item.kind,x,y,item.x,item.y))continue;
      return true;
    }
    // Block against the ARTWORK, not the collision footprint. `radius` is the
    // steering footprint and is deliberately smaller than the sprite: a crystal
    // has radius 12 but is drawn 57 px wide, so blocking only 12 px from the
    // centre let the spider walk straight over the visible crystal.
    const r=decorArtExtent(item.kind);
    if(dx*dx+dy*dy>r*r)continue;
    return true;
  }
  return false;
}
/** Nearest blocking item within range, for steering. Null when clear. */
export function decorNearestBlocking(s,x,y,maxDistance){
  let best=maxDistance*maxDistance,found=null;
  for(const item of s.decor){
    if(item.kind===DECOR_EMPTY||!decorPathBlocks(item.blocking))continue;
    const dx=item.x-x,dy=item.y-y,d2=dx*dx+dy*dy;
    if(d2<best){best=d2;found=item;}
  }
  return found?{x:found.x,y:found.y,kind:found.kind,distance:Math.sqrt(best)}:null;
}
/** Petting: a tap that lands on the spider's body. Returns true when the tap was
 * consumed as a pet. The cooldown exists because a tap is a cheap gesture:
 * without it a player could hold a finger on the spider and farm mood, XP and
 * the daily task. */
export function pet(s,x,y){
  const dx=x-s.spider_x,dy=y-s.spider_y;
  if(dx*dx+dy*dy>C.PET_RADIUS_PX*C.PET_RADIUS_PX)return false;
  const r=runtime(s),now=r.now_ms;
  // A separate "has petted" flag, not `last_pet !== 0`: the clock can
  // legitimately read 0 early in a session, and then the guard would never
  // engage and a held finger could farm mood.
  if(r.petted_once&&now-r.last_pet<C.PET_COOLDOWN_MS)return false;
  companionEnsure(s);
  r.petted_once=true;
  r.last_pet=now;
  s.mood=clamp(s.mood+C.PET_MOOD_GAIN);
  s.pets++;
  if(s.bond<100)s.bond++;
  // Affection is uncapped; the currency is not. A tap is the cheapest gesture
  // in the game, so an uncapped reward turned a bored thumb into an XP faucet.
  const today=dailyToday(s);
  if(s.pet_xp_day!==today){s.pet_xp_day=today;s.pet_xp_today=0;}
  if(s.pet_xp_today<C.PET_XP_DAILY_MAX){
    const paid=Math.min(C.PET_XP,C.PET_XP_DAILY_MAX-s.pet_xp_today);
    s.pet_xp_today+=paid;addXP(s,paid);
  }
  setState(s,7);r.oneshot_until=now+180;
  r.pet_react_until=now+C.PET_REACT_MS;
  awardAchievement(s,'first_pet');
  companionNote(s,1);
  syncView(s);
  return true;
}
export function care(s,action) {
  const r=runtime(s);
  if(action==='feed')return feed(s);
  if(action==='clean'){
    // The guard is "is there anything to do", not "is there XP to pay": a tank
    // holding only bugs scores 0 XP but Clean still shoos them out.
    if(!cleanNeeded(s))return false;
    companionEnsure(s);
    const xp=cleanReward(s);
    s.hygiene=C.CLEAN_HYGIENE_SET;s.cleans++;s.poop=[];s.prey=[];
    addXP(s,xp);awardAchievement(s,'first_clean');
    companionNote(s,6);
  }else if(action==='play'){
    s.mood=clamp(s.mood+10);s.energy=clamp(s.energy-5);s.plays++;
    setState(s,7);r.oneshot_until=r.now_ms+180;
    addXP(s,4);awardAchievement(s,'first_play');
  }else if(action==='sleep'){
    r.sleep_forced=s.state!==6;r.sleep_start=r.now_ms;setState(s,r.sleep_forced?6:0);
  }else throw new Error('Unknown care action');
  syncView(s);return true;
}
export function giftOwned(s,id){
  return Number.isInteger(id)&&id>=0&&id<GIFTS.length&&
    !!((id<32?s.gifts_owned:s.catalogue_owned_high)&(1<<(id%32)));
}
function markGiftOwned(s,id){
  const field=id<32?'gifts_owned':'catalogue_owned_high';
  s[field]=(s[field]|(1<<(id%32)))>>>0;
}
export function purchaseGift(s,id){
  const d=GIFTS[id];if(!d)return event(s,'rejected',{reason:'unknown gift'});
  if(giftOwned(s,id))return true;
  if(s.level<d.level||s.silk<d.cost)return event(s,'rejected',{reason:s.level<d.level?'gift locked':'not enough Silk'});
  s.silk-=d.cost;markGiftOwned(s,id);return true;
}
export function placeGift(s,id,x=315,y=330) {
  if(!purchaseGift(s,id))return false;
  s.gift={kind:id,variant:0,used:1,x:round(clamp(x,34,446)),y:round(clamp(y,34,446))};return true;
}
export function moveGift(s,x,y) {
  if(!s.gift.used)return event(s,'rejected',{reason:'no placed gift'});
  s.gift.x=round(clamp(x,34,446));s.gift.y=round(clamp(y,34,446));
  runtime(s).gift_visiting=false;return true;
}
export function giftHitTest(s,x,y) {
  return !!s.gift.used&&Math.abs(x-s.gift.x)<=32&&Math.abs(y-s.gift.y)<=32;
}
export function removeGift(s){s.gift.used=0;runtime(s).gift_visiting=false;return true;}
export function visitGift(s){
  const r=runtime(s);
  if(!canVisit(s))return event(s,'rejected',{reason:'gift visit unavailable'});
  r.gift_visit_at=r.now_ms||1;return true;
}
/* The play menu offers five games. The first three are the original
 * tap-and-match games; the last two are spider-themed and use the same
 * round/score/reward plumbing. Mirrors PLAY_KINDS in ui_menu.c. */
export const PLAY_KINDS=5;
export const PLAY_NAMES=['Bug Hunt','Color Match','Quick Tap','Web Weave','Pounce'];
export function minigameReward(s,kind,score) {
  // ui_menu callback takes score only; accept (state, score) too.
  const completedKind=score!==undefined&&Number.isInteger(kind)&&kind>=0&&kind<PLAY_KINDS?kind:null;
  if(score===undefined)score=kind;
  if(score&&typeof score==='object')score=score.score;
  score=integer(score,0,8);
  companionEnsure(s);
  const before=s.xp;care(s,'play');addXP(s,2+score*4);addSilk(s,score*3);
  if(completedKind!==null)companionNote(s,2,completedKind);
  return {xp:s.xp-before,silk:score*3,score};
}
export function aimAvailable(s){return ![4,5,6,8].includes(s.state)&&!runtime(s).pounce&&preyCount(s)>0&&s.hunger<95;}
export function aimWindow(s) {
  let w=Math.max(1600,2600-s.level*20);
  w=Math.trunc(w*giftBonus(s).aim_window_mult);
  w=Math.trunc(w*temperament(s).aim_window_mult);
  return s.bond>=50?Math.trunc(w*1.15):w;
}
function nearest(s,x=s.spider_x,y=s.spider_y) {
  let best=null,d=Infinity;
  for(const p of s.prey)if(p.alive&&!p.dead){const pd=Math.hypot(p.x-x,p.y-y);if(pd<d){best=p;d=pd;}}
  return best?{prey:best,d}:null;
}
function nearestAim(s){
  const n=nearest(s);
  if(!n)return null;
  const x=Math.trunc(n.prey.x),y=Math.trunc(n.prey.y);
  return {x,y,d:Math.hypot(x-s.spider_x,y-s.spider_y)};
}
export function beginAim(s,x,y){if(!aimAvailable(s))return event(s,'rejected',{reason:'aim unavailable'});
  runtime(s).drag={x,y,start_x:x,start_y:y};return true;}
export function moveAim(s,x,y){if(!aimAvailable(s)||!runtime(s).drag)return false;Object.assign(runtime(s).drag,{x,y});return true;}
export function cancelAim(s){runtime(s).drag=null;}
export function aimInfo(s) {
  const r=runtime(s),max=160*giftBonus(s).pounce_range_mult,near=nearestAim(s),drag=r.drag;
  return {available:aimAvailable(s),dragging:!!drag&&aimAvailable(s),
    dx:drag?drag.x-s.spider_x:0,dy:drag?drag.y-s.spider_y:0,
    pull:drag?Math.min(1,Math.hypot(drag.x-s.spider_x,drag.y-s.spider_y)/max):0,
    ideal:near?Math.min(1,near.d/max):1,range:max,total_ms:aimWindow(s),
    remaining_ms:r.aim_active?Math.max(0,aimWindow(s)-(r.now_ms-r.aim_start)):0};
}
export function endAim(s,x,y) {
  const r=runtime(s);if(x!==undefined)moveAim(s,x,y);
  const d=r.drag;
  if(!aimAvailable(s)||!d){r.drag=null;return false;}r.drag=null;
  if(Math.hypot(d.x-d.start_x,d.y-d.start_y)<18)return false;
  const dx=d.x-s.spider_x,dy=d.y-s.spider_y,length=Math.hypot(dx,dy);if(length<18)return false;
  const max=160*giftBonus(s).pounce_range_mult,dist=Math.min(length,max);
  r.aim_power=dist/max;r.aim_ideal=aimInfo(s).ideal;
  launch(s,s.spider_x+Math.trunc(dx/length*dist),s.spider_y+Math.trunc(dy/length*dist),true);
  return true;
}
export function pounce(s,x,y){
  if(!beginAim(s,s.spider_x,s.spider_y))return false;
  return endAim(s,x,y);
}
function launch(s,x,y,player) {
  const r=runtime(s),b=giftBonus(s);x=Math.trunc(clamp(x,34,446));y=Math.trunc(clamp(y,34,446));
  const flight=Math.min(Math.max(120,Math.trunc(Math.hypot(x-s.spider_x,y-s.spider_y)/260*1000)),Math.trunc(160*b.pounce_range_mult/260*1000)+40);
  s.energy=clamp(s.energy-Math.ceil(3/b.movement_efficiency_mult));r.next_pounce_at=r.now_ms+1400;
  setState(s,4);r.pounce={x,y,player,end:r.now_ms+flight,start:r.now_ms,duration:flight};
  syncView(s);
}
export function seekTo(s,x,y){setState(s,1);runtime(s).target={x:clamp(x,34,446),y:clamp(y,34,446)};}
export function noteTap(s,x,y){if(x!==s.spider_x||y!==s.spider_y)runtime(s).tap_heading=Math.atan2(y-s.spider_y,x-s.spider_x);}
export function resolvePounce(s,player=runtime(s).pounce?.player??true) {
  companionEnsure(s);
  const near=nearest(s);if(!near)return event(s,'miss',{reason:'no prey'});
  const {prey:p,d}=near,edge=Math.max(0,d-22),b=giftBonus(s),r=runtime(s);
  if(edge>CONFIG.CATCH_GOOD_PX){s.streak=0;addXP(s,1);return event(s,'miss',{reason:'out of reach',distance:d});}
  let probability=(.55-edge*.004+s.energy/100*.2+s.stage/2*.1)*(s.sick?.7:1)*(player?1.15:.55)*temperament(s).catch_mult*b.catch_success_mult*b.pounce_success_mult;
  probability=clamp(probability,.2,.95);
  let perfect=edge<=CONFIG.CATCH_PERFECT_PX*SPECIES[p.species].perfect_mult;
  if(!perfect&&player&&Math.abs(r.aim_power-r.aim_ideal)<=.1)perfect=true;
  const caught=perfect||random(s)<=probability;
  if(!caught){p.fleeing=true;p.retarget_at=0;s.streak=0;addXP(s,1);return event(s,'miss',{distance:d,probability});}
  // Native consumes a roll even for guaranteed geometric perfect catches.
  if(perfect)random(s);
  p.dead=true;p.fleeing=false;p.frame=0;p.dead_at=r.now_ms+2200;
  s.state=5;s.eat_until_ms=r.now_ms+800;
  // Species nutrition scales the meal: a waxworm is worth far more than a fly.
  const sp=SPECIES[p.species];
  s.hunger=clamp(s.hunger+Math.round((p.wild?8:15)*sp.hunger_mult)+(perfect?5:0));
  s.mood=clamp(s.mood+Math.round((p.wild?4:8)*sp.mood_mult)+(perfect?4:0));
  s.energy=clamp(s.energy+Math.round(6*sp.energy_mult));
  if(perfect){s.streak=(s.streak+1)&65535;s.best_streak=Math.max(s.best_streak,s.streak);awardAchievement(s,'first_perfect');}
  const xp=Math.trunc((perfect?18:10)*(1+Math.min(10,s.streak)*.05));
  addXP(s,xp);addSilk(s,perfect?5:3);s.meals++;
  awardAchievement(s,'first_meal');awardAchievement(s,'first_hunt');
  if(s.meals>=10)awardAchievement(s,'ten_meals');
  s.bond=clamp(s.bond+(perfect?4:2));s.hygiene=clamp(s.hygiene-12);
  addPoop(s,s.spider_x+roll(s,17)-8,s.spider_y+roll(s,17)-8);
  s.lastEvent={type:'catch',at:r.now_ms,quality:perfect?'perfect':'good',probability,distance:d,xp,silk:perfect?5:3,species:p.key};
  if(player)companionNote(s,3,p.species);
  return true;
}
export function recomputeMood(s) {
  let base=Math.trunc((s.hunger+s.hygiene+s.energy)/3),penalty=s.sick?6:12;
  for(const k of ['hunger','hygiene','energy'])if(s[k]<25)base-=penalty;
  base=Math.trunc(base*giftBonus(s).mood_mult);
  s.mood=clamp(s.mood+Math.trunc((base-s.mood)/4));
}
function needsTick(s) {
  checkAchievements(s);
  const r=runtime(s),now=r.now_ms,b=giftBonus(s);
  // Establish the task baseline for the current day. A no-op once the day
  // matches, and it covers a migrated or restored save that has no baseline.
  dailyEnsureBase(s);
  if(now-r.last_age>=30000){
    r.last_age=now;s.age_ticks++;
    const days=Math.floor(s.age_ticks/2880);
    if(days!==s.days_lived){s.days_lived=days;addXP(s,10);
      const stage=days>=5?2:days>=2?1:0;
      if(stage!==s.stage){s.stage=stage;s.last_molt_day=days;setState(s,8);awardAchievement(s,'first_molt');if(stage===2)awardAchievement(s,'adult');}
      // Snapshot the daily task baseline the moment the day turns over, before
      // the player can act on it.
      dailyEnsureBase(s);
    }
    if(days-s.last_molt_day>=2&&s.stage!==2){s.last_molt_day=days;setState(s,8);addXP(s,8);awardAchievement(s,'first_molt');}
  }
  if(now-r.last_decay<3000)return;r.last_decay=now;
  // Needs decay faster as the spider ages. The scale is deliberately not shown
  // in the UI: the player feels a growing spider, not a spreadsheet.
  const ageScale=needsAgeScale(s);
  r.hunger_acc+=multiply(3,.05,temperament(s).hunger_decay_mult,b.all_stats_decay_mult,ageScale);
  if(r.hunger_acc>=1){const n=Math.trunc(r.hunger_acc);r.hunger_acc-=n;s.hunger=clamp(s.hunger-n);}
  if(s.sick)s.bond=clamp(s.bond-1);
  r.hygiene_acc=f32(r.hygiene_acc+(s.poop.length?multiply(.7,.05,b.hygiene_decay_mult,b.all_stats_decay_mult,ageScale):-multiply(2,.05)));
  r.energy_acc=f32(r.energy_acc+(s.state===6?-multiply(6,.05,b.energy_regen_mult):f32(multiply(.5,.05,b.all_stats_decay_mult,ageScale)/f32(b.movement_efficiency_mult))));
  for(const key of ['hygiene','energy'])if(Math.abs(r[`${key}_acc`])>=1){
    const n=Math.trunc(r[`${key}_acc`]);r[`${key}_acc`]=f32(r[`${key}_acc`]-n);s[key]=clamp(s[key]-n);
  }
  recomputeMood(s);
  /* The spider's own droppings. A catch used to be the only source, so a spider
   * that was fed and left alone never dirtied its tank. It now drops one every
   * SPIDER_POOP_MS while awake and still holding something to pass. */
  if(s.state!==6&&s.hunger>=20){
    if(!r.poop_at)r.poop_at=now+420000;
    else if(now>=r.poop_at){
      r.poop_at=now+420000;
      addPoop(s,s.spider_x+roll(s,17)-8,s.spider_y+roll(s,17)-8);
    }
  } else if(r.poop_at) r.poop_at=now+420000;
  /* Auto Clean: opt-in, and deliberately not a second Clean button. It sweeps
   * droppings but pays no XP and no achievement, so leaving it on is a
   * convenience rather than a way to farm the manual action. It also leaves
   * live prey alone: an automatic sweep that removed a bug the player had just
   * fed would fight the Feed button. */
  if(s.auto_clean&&((s.poop?.length??0)>0||s.hygiene<C.CLEAN_HYGIENE_SET)){
    if(!r.auto_clean_at)r.auto_clean_at=now+180000;
    else if(now>=r.auto_clean_at){
      r.auto_clean_at=now+180000;
      s.hygiene=C.CLEAN_HYGIENE_SET;s.poop.length=0;
    }
  } else r.auto_clean_at=0;
  if((s.hunger<10||s.mood<10)&&!s.sick){s.sick=1;setState(s,9);}
  else if(s.sick&&s.hunger>25&&s.mood>25){s.sick=0;setState(s,0);awardAchievement(s,'survived_sick');}
}
function mindTick(s){
  const r=runtime(s);if(r.now_ms<r.next_mind)return;r.next_mind=r.now_ms+4000;
  /* Environment probes, cached once per evaluation rather than per rule. */
  const climbable=s.decor.some(d=>d.kind!==DECOR_EMPTY&&decorClimbable(d.kind));
  const giftPlaced=Boolean(s.gift?.used);
  const period=dayPeriod(s);
  let best=null;
  for(const row of MIND_RULES){
    const [,want,weight,h,e,m,y,b,p,per,climb,gift]=row;
    if(weight<0)continue;
    if((h>=0&&s.hunger>=h)||(e>=0&&s.energy>=e)||(m>=0&&s.mood>=m)||(y>=0&&s.hygiene>=y)||(b>=0&&s.bond<=b)||(p===1&&!preyCount(s))||(p===0&&preyCount(s)))continue;
    /* A period rule needs a real clock. Without one the period is 255, which
     * matches no specific period, so time-of-day behaviour is absent rather
     * than wrong. */
    if(per>=0&&period!==per)continue;
    if(climb===1&&!climbable)continue;
    if(climb===0&&climbable)continue;
    if(gift===1&&!giftPlaced)continue;
    if(gift===0&&giftPlaced)continue;
    if(!best||weight>best[2])best=row;
  }
  const want=best?.[1]??'nothing';
  /* Only restart the dwell timer when the INTENT changes, not when the rule
   * that produced it changes: two rules can both want 'explore', and resetting
   * as they swap would keep the spider from ever acting on either. */
  if(want!==s.want)r.mind_since=r.now_ms;
  s.mind_rule=best?.[0]??null;s.want=want;
  s.thought={food:'hungry',rest:'sleepy',play:'wants to play',clean:'feels grubby',
    explore:'exploring',watch:'watching you',hide:'hiding',climb:'wants to climb',
    perch:'looking for a perch',visit:'visiting its gift',sun:'basking',nothing:''}[want];
}
/* The current day period, mirroring life_period_now(): 0 morning, 1 afternoon,
 * 2 evening, 3 night, 255 when the clock is unavailable. */
function dayPeriod(s){
  const epoch=s.epoch??0;
  if(!(epoch>=1704067200))return 255;
  const hour=Math.floor((epoch%86400)/3600);
  return hour<6||hour>=22?3:hour<12?0:hour<18?1:2;
}
// The gift is always walked over.
//
// It used to have a path control like decor, offering Walk Over and Solid. Solid
// bought nothing the player could see -- the gift is a single 64x64 object with
// no depth, so "the spider goes around it" read as an invisible wall around a
// flat sprite -- and it cost a fragile coupling: Solid pushed the spider out to
// GIFT_PUSH_RADIUS while the visit only counted as arrived inside
// GIFT_VISIT_ARRIVE, so the two constants had to stay in the right order or a
// visit would shove the spider out every tick and circle forever.
//
// Removing the choice removes the coupling. There is no push radius, no arrival
// threshold to keep in sync, and no avoidance steering to tune: the spider
// crosses the gift, which is also what the artwork reads as.
export const GIFT_VISIT_ARRIVE=18;
// Kept as a named constant because the visit branch is the one place that can
// drive the spider onto the gift, and the arrival test reads better named.
export function giftBlocksMovement(){
  return false;
}
function steer(s,want,dt,avoid=false){
  if(avoid){
    let a=0;
    // Blocking decor: same steering principle, measured from the edge the
    // spider is actually blocked at, so a big log pushes it away further out
    // than a small rock. Walk Behind blocks the whole artwork, so measuring
    // from the small collision radius would let the spider walk into the
    // sprite before it started turning. Non-blocking items are ignored.
    const near=decorNearestBlocking(s,s.spider_x,s.spider_y,66);
    if(near){
      const slot=s.decor.findIndex(d=>d.kind!==DECOR_EMPTY&&
        decorPathBlocks(d.blocking)&&d.x===near.x&&d.y===near.y);
      const blockR=slot>=0?decorBlockRadius(s,slot):0;
      const edge=near.distance-blockR;
      if(edge<=26&&near.distance>.1){
        const dx=near.x-s.spider_x,dy=near.y-s.spider_y;
        if((dx*Math.cos(want)+dy*Math.sin(want))/near.distance>=.15){
          const w=Math.max(0,1-edge/26);
          a+=(wrap(Math.atan2(dy,dx)-want)>=0?-1:1)*w*PI*.6;
        }
      }
    }
    let px=0,py=0;
    const dl=s.spider_x-34,dr=446-s.spider_x,top=s.spider_y-34,bot=446-s.spider_y;
    if(dl<70)px+=1-dl/70;if(dr<70)px-=1-dr/70;if(top<70)py+=1-top/70;if(bot<70)py-=1-bot/70;
    if(Math.cos(s.spider_heading)*px+Math.sin(s.spider_heading)*py<0)
      a+=wrap(Math.atan2(py,px)-want)*Math.min(1,Math.hypot(px,py));
    want=wrap(want+a);
  }
  const max=540*PI/180*dt;
  s.spider_heading=wrap(s.spider_heading+clamp(wrap(want-s.spider_heading),-max,max));
}
function advance(s,speed,dt){
  const r=runtime(s);
  const nx=clamp(r.px+Math.cos(s.spider_heading)*speed*dt,34,446);
  const ny=clamp(r.py+Math.sin(s.spider_heading)*speed*dt,34,446);
  // Steering normally keeps the spider out of blocking decor, but the player
  // can drop an item straight onto it. Refuse that step rather than let the
  // spider stand inside a rock; it steers out on the next frame.
  if(decorBlocksPoint(s,round(nx),round(ny)))return;
  r.px=nx;r.py=ny;
  s.spider_x=round(r.px);s.spider_y=round(r.py);
}
function inward(s) {
  // Whole playfield, not the middle half: aiming at the middle made every
  // wander leg converge on the centre. 0.40 of the half-extent reaches most of
  // the tank while staying off the wall.
  const tx=240+(roll(s,2001)/1000-1)*165,ty=240+(roll(s,2001)/1000-1)*165;
  return Math.atan2(ty-s.spider_y,tx-s.spider_x);
}
function canVisit(s){return s.gift.used&&!s.sick&&s.energy>=20&&s.hunger>=60&&[0,1].includes(s.state);}
// True when the spider is standing on a small prop AND nothing more urgent is
// happening. Both the IDLE and WANDER triggers must use this: the IDLE trigger
// was originally unguarded, so the moment PERCH left because hunger dropped,
// IDLE re-perched it on the same tick and the two states alternated at the
// 20 Hz logic rate. The visible symptom was a spider sitting on the prop and
// vibrating, because the foreshortening eased in and out on every flip.
function perchWanted(s,t){
  if(s.sick)return false;
  if(s.energy<t.sleep_threshold)return false;
  if(s.hunger<40)return false;
  // A cooldown after leaving. Without it the spider re-perched on the very next
  // tick, so the state flickered PERCH -> IDLE -> PERCH and the foreshortening
  // pulsed. Leaving has to mean leaving for a moment.
  const r=runtime(s);
  if(r.now_ms<r.perch_cooldown_until)return false;
  return decorPerchAt(s,s.spider_x,s.spider_y);
}
function behaviourTick(s,dt){
  const r=runtime(s),now=r.now_ms,b=giftBonus(s),t=temperament(s);
  if(r.drag&&aimAvailable(s))return;
  if(!s.auto_hunt&&[2,3].includes(s.state))setState(s,0);
  const scale=(s.sick?.45:1)*(s.stage===0?.75:s.stage===2?1.15:1)*t.speed_mult;
  if(!canVisit(s)){r.gift_visiting=false;r.gift_hold_until=0;r.gift_visit_at=now+15000;}
  else {
    if(!r.gift_visit_at)r.gift_visit_at=now+15000;
    if(!r.gift_visiting&&now>=r.gift_visit_at){r.gift_visiting=true;r.gift_visit_end=now+20000;r.gift_hold_until=0;}
    if(r.gift_visiting){
      const d=Math.hypot(s.gift.x-s.spider_x,s.gift.y-s.spider_y);
      if(d>GIFT_VISIT_ARRIVE&&!r.gift_hold_until){
        setState(s,1);const want=Math.atan2(s.gift.y-r.py,s.gift.x-r.px);
        s.spider_heading=wrap(s.spider_heading+wrap(want-s.spider_heading)*.15);advance(s,55*scale,dt);
      }else if(!r.gift_hold_until){setState(s,0);r.gift_hold_until=now+3000;}
      else if(now>=r.gift_hold_until){s.mood=clamp(s.mood+4);s.energy=clamp(s.energy+4);s.visits++;companionNote(s,4,s.gift.kind);r.gift_visiting=false;r.gift_visit_at=now+(s.companion.favorite_gift===s.gift.kind?45000:60000);}
      if(now>=r.gift_visit_end){r.gift_visiting=false;r.gift_visit_at=now+60000;}
      return;
    }
  }
  switch(s.state){
    case 6:
      /* A player-initiated sleep holds until the spider is actually rested,
       * with a 6 s floor so the button always visibly works. Waking at a fixed
       * 95 meant a spider at 80 energy slept for six seconds and stood up
       * again, which is why the button read as pointless.
       *
       * An exhaustion sleep wakes at 60, well above the temperament threshold
       * that caused it. Waking at the same value the spider fell asleep at made
       * the two rules oscillate every tick, and because the eyes are drawn
       * black while asleep the symptom was a spider whose eyes flickered. */
      if(r.sleep_forced){
        if(now-r.sleep_start>=6000&&s.energy>=100){r.sleep_forced=false;setState(s,0);}
      }
      else if(s.sick?s.energy>=30:s.energy>=60||(s.hunger<25&&s.energy>=t.sleep_threshold+5))setState(s,0);
      break;
    case 5:if(now>=s.eat_until_ms)setState(s,0);break;
    case 8:if(now>=r.oneshot_until)setState(s,0);break;
    case 7:if(now>=r.oneshot_until&&now>r.idle_until)setState(s,0);break;
    case 4:{
      const p=r.pounce;if(!p){setState(s,0);break;}
      const dx=p.x-s.spider_x,dy=p.y-s.spider_y,d=Math.hypot(dx,dy);
      if(d>2){s.spider_heading=Math.atan2(dy,dx);const step=Math.min(d,260*dt);
        s.spider_x=Math.trunc(s.spider_x+dx/d*step);s.spider_y=Math.trunc(s.spider_y+dy/d*step);r.px=s.spider_x;r.py=s.spider_y;}
      if(now>=p.end){
        /* A pounce is ballistic, so it can land inside a solid item. The walk
         * path refuses those steps, but the landing was written straight into
         * the position, which is why the spider appeared to stand on top of
         * blocking decor. Push the landing back out first. */
        let lx=p.x,ly=p.y;
        if(decorBlocksPoint(s,lx,ly)){
          const near=decorNearestBlocking(s,lx,ly,200);
          if(near){
            const ax=lx-near.x,ay=ly-near.y,ad=Math.hypot(ax,ay)||1;
            const rad=(DECOR_KINDS[near.kind]?.radius??16)+4;
            lx=round(near.x+ax/ad*rad);ly=round(near.y+ay/ad*rad);
          }
        }
        s.spider_x=lx;s.spider_y=ly;r.px=lx;r.py=ly;resolvePounce(s,p.player);r.pounce=null;if(s.state===4)setState(s,0);
      }
      break;
    }
    case 2:{
      const near=nearestAim(s);if(!near){setState(s,1);break;}
      steer(s,Math.atan2(near.y-s.spider_y,near.x-s.spider_x),dt,true);advance(s,55*.55*scale,dt);
      if(near.d<110*b.prey_detection_range_mult&&now>=r.next_pounce_at){setState(s,3);r.aim_active=true;r.aim_start=now;}
      break;
    }
    case 3:{
      const near=nearestAim(s);if(!near){setState(s,0);break;}
      steer(s,Math.atan2(near.y-s.spider_y,near.x-s.spider_x),dt);
      r.aim_ideal=Math.min(1,near.d/(160*b.pounce_range_mult));
      if(now-r.aim_start>=aimWindow(s))launch(s,near.x+roll(s,25)-12,near.y+roll(s,25)-12,false);
      break;
    }
    case 9:
      if(s.energy<12){setState(s,6);break;}
      if(s.auto_hunt&&preyCount(s)){setState(s,2);break;}
      if(now>=r.retarget_at){r.retarget_at=now+4000;r.wander_dir=inward(s);}
      steer(s,r.wander_dir,dt,true);advance(s,55*.35,dt);break;
    case 1:
      if(s.auto_hunt&&preyCount(s)&&s.hunger<95&&!s.sick){setState(s,2);break;}
      /* Perching has to be checked here as well as in IDLE. A wandering spider
       * crosses a small prop in well under a second, so an IDLE-only check
       * meant it walked straight over the books and the player never saw a
       * perch. */
      if(perchWanted(s,t)){
        r.perch_heading=null;r.perch_until=now+2500+roll(s,2500);setState(s,10);break;
      }
      /* A wander with no destination is the spider pottering about, which is
       * exactly when a new intent should be able to take over. Without this the
       * mind was only consulted from IDLE, so a spider that had started
       * wandering never reconsidered and the brain looked inert. */
      if(!r.target&&actOnIntent(s,now))break;
      if(r.target){
        if(Math.hypot(r.target.x-s.spider_x,r.target.y-s.spider_y)<12){setState(s,0);break;}
        steer(s,Math.atan2(r.target.y-s.spider_y,r.target.x-s.spider_x),dt,true);
      }else {if(now>=r.retarget_at){r.retarget_at=now+2000;r.wander_dir=inward(s);}steer(s,r.wander_dir,dt,true);}
      advance(s,55*scale,dt);
      if(Math.abs(r.px-r.stuck_x)<1.5&&Math.abs(r.py-r.stuck_y)<1.5){
        if(now-r.stuck_at>1500){r.wander_dir=inward(s);r.target=null;r.stuck_at=now;r.stuck_x=r.px;r.stuck_y=r.py;}
      }else {r.stuck_at=now;r.stuck_x=r.px;r.stuck_y=r.py;}
      break;
    case 10:{
      /* Perched on a small climbable prop. The spider holds the item's centre
       * and faces out from it; the pose sells the height, not the lift. */
      const want=decorPerchHeading(s,s.spider_x,s.spider_y);
      if(want===null){r.perch_heading=null;setState(s,0);break;}
      const centre=decorPerchCenter(s,s.spider_x,s.spider_y);
      if(centre){r.perch_x=centre.x;r.perch_y=centre.y;}
      const dx=r.perch_x-s.spider_x,dy=r.perch_y-s.spider_y,d=Math.hypot(dx,dy);
      if(d>1){const step=Math.min(d,40*dt);
        s.spider_x=Math.trunc(s.spider_x+dx/d*step);s.spider_y=Math.trunc(s.spider_y+dy/d*step);
        r.px=s.spider_x;r.py=s.spider_y;}
      if(r.perch_heading===null)r.perch_heading=want;
      const delta=wrap(r.perch_heading-s.spider_heading);
      s.spider_heading=Math.abs(delta)>.02?wrap(s.spider_heading+delta*.18):r.perch_heading;
      // A forced state change (console `state`, or a save load that restores
      // PERCH) never runs the trigger that sets the timer, so the perch would
      // expire on the first tick. Start a fresh window instead.
      if(!r.perch_until)r.perch_until=now+2500+roll(s,2500);
      const urgent=s.sick||s.energy<t.sleep_threshold||s.hunger<40||(s.auto_hunt&&preyCount(s)&&s.hunger<95);
      if(urgent||now>=r.perch_until){
        r.perch_heading=null;
        // Hold off re-perching so the state does not flicker on consecutive ticks.
        r.perch_cooldown_until=now+4000;
        setState(s,0);
      }
      break;
    }
    default:{
      const done=now>=r.idle_until;if(done)r.idle_until=now+1200+roll(s,1800);
      if(s.bond>=25&&r.tap_heading!==null)s.spider_heading=wrap(s.spider_heading+wrap(r.tap_heading-s.spider_heading)*.12);
      if(s.sick)setState(s,9);
      else if(s.energy<t.sleep_threshold)setState(s,6);
      else if(perchWanted(s,t)){
        /* Standing on a small climbable prop: sit on it and turn to face out.
         * Checked before the wander branch so the spider does not immediately
         * walk off the thing it just climbed. */
        r.perch_heading=null;
        r.perch_until=now+2500+roll(s,2500);setState(s,10);
      }
      else if(s.mood<20&&s.hunger>40){if(done)setState(s,1);}
      else if(s.auto_hunt&&preyCount(s)&&s.hunger<95)setState(s,2);
      else if(done&&actOnIntent(s,now)){/* the mind chose something to do */}
      else if(done)setState(s,1);
    }
  }
  if(s.spider_x!==round(r.px))r.px=s.spider_x;if(s.spider_y!==round(r.py))r.py=s.spider_y;
  s.spider_x=clamp(s.spider_x,34,446);s.spider_y=clamp(s.spider_y,34,446);
}
/* Act on the mind's current intent, mirroring act_on_intent() in behaviour.c.
 *
 * This is the bridge that makes the brain matter: without it the rules engine
 * computed an intent every four seconds and nothing ever read it, so the spider
 * behaved exactly as it did before the mind existed.
 *
 * Returns true when the intent took over the decision. Every branch is guarded
 * by the same safety conditions the FSM already used, so an intent can never
 * override hunger, exhaustion or sickness -- those are handled before this is
 * called. */
function actOnIntent(s,now){
  const r=runtime(s);
  /* Give an intent a moment to settle before acting on it. A creature that
   * changes its mind every four seconds reads as twitchy. */
  if(now-(r.mind_since??0)<2500)return false;
  switch(s.want){
    case 'climb':{
      if(s.sick||s.energy<30||s.hunger<40)return false;
      let best=null,bestD=0;
      for(const d of s.decor){
        if(d.kind===DECOR_EMPTY||!decorClimbable(d.kind))continue;
        const dd=Math.hypot(d.x-s.spider_x,d.y-s.spider_y);
        if(!best||dd<bestD){best=d;bestD=dd;}
      }
      if(!best||bestD<24)return false;
      seekTo(s,best.x,best.y);
      return true;
    }
    case 'visit':{
      if(!s.gift?.used||s.energy<20||s.hunger<60)return false;
      if(Math.hypot(s.gift.x-s.spider_x,s.gift.y-s.spider_y)<40)return false;
      seekTo(s,s.gift.x,s.gift.y);
      return true;
    }
    case 'hide':{
      const cx=(field.left+field.right)/2,cy=(field.top+field.bottom)/2;
      seekTo(s,s.spider_x<cx?field.left+40:field.right-40,
               s.spider_y<cy?field.top+40:field.bottom-40);
      return true;
    }
    case 'sun':
    case 'watch':
      /* Both are "settle and be still". The FSM has no separate state for them,
       * and inventing one would need new artwork; holding IDLE with a longer
       * dwell is the honest expression of the same idea. */
      r.idle_until=now+4000+roll(s,4000);
      return false;
    default:
      return false;
  }
}
function automationTick(s){
  const r=runtime(s),now=r.now_ms;
  if(!s.auto_feed){r.wild_seeded=false;r.last_wild=now;r.last_visitor_day=s.days_lived;return;}
  /* Auto Feed holds hunger near 50 rather than topping the tank up on a fixed
   * timer. The old rule spawned one wild bug every 20 min and only when the
   * tank was empty; hunger decays at 3/min, so 20 minutes cost 60 points and
   * the spider was permanently starving. The interval is now derived from the
   * gap between the current hunger and the target. */
  if(!r.wild_seeded){r.wild_seeded=true;r.last_wild=now;
    /* Turning Auto Feed on puts a bug in straight away, so the switch has a
     * visible effect even when the spider is not hungry yet. */
    if(!preyCount(s))spawnPrey(s,chooseSpecies(s),true);}
  else if(!preyCount(s)&&s.hunger<=50){
    const deficit=50-s.hunger;
    let wait=Math.trunc(1200000*8/(deficit+8)/(1+giftBonus(s).wild_spawn_bonus));
    if(wait<60000)wait=60000;
    if(now-r.last_wild>=wait){r.last_wild=now;spawnPrey(s,chooseSpecies(s),true);}
  } else if(preyCount(s)) r.last_wild=now;
  if(s.days_lived!==r.last_visitor_day){
    r.last_visitor_day=s.days_lived;
    if(s.days_lived>0)spawnPrey(s,SPECIES.filter(p=>speciesUnlocked(s,p.id)).at(-1).id,true);
  }
}
function preyTick(s,dt){
  const now=runtime(s).now_ms,b=giftBonus(s),speed=Math.min(1.8,1+s.level*.06),alert=Math.min(1.6,1+s.level*.05);
  for(const p of s.prey){
    if(!p.alive)continue;
    if(p.dead){if(p.dead_at&&now>=p.dead_at){p.alive=false;p.dead=false;p.dead_at=0;}continue;}
    // A bug left in the tank fouls it: a pale speckle appears near the bug every
    // BUG_POOP_MS, which dirties the tank until Clean is used.
    if(p.poop_at&&now>=p.poop_at){
      p.poop_at=now+C.BUG_POOP_MS;
      addBugPoop(s,p.x+roll(s,13)-6,p.y+roll(s,13)-6);
    }
    if(runtime(s).drag&&aimAvailable(s))continue;
    const oldX=p.x,oldY=p.y,sp=SPECIES[p.species],dx=p.x-s.spider_x,dy=p.y-s.spider_y,d=Math.hypot(dx,dy);
    const fleeR=Math.min(102,70*alert*sp.flee_radius_mult*(p.wild?1.25:1)*b.fear_mult);
    if(d<fleeR&&!s.sick)p.fleeing=true;else if(d>fleeR*1.25)p.fleeing=false;
    let step;
    if(p.fleeing){
      p.heading=wrap(p.heading+wrap(Math.atan2(dy,dx)-p.heading)*.35);
      step=now%1300<300?70*speed*sp.flee_speed_mult*(p.wild?1.15:1)*b.prey_flee_penalty*dt:0;
    }else{
      if(now>=p.retarget_at){p.retarget_at=now+(sp.move==='erratic'?600:1500);p.wander_dir=random(s)*TAU-PI;}
      p.heading=wrap(p.heading+wrap(p.wander_dir-p.heading)*.12);
      step=30*speed*sp.speed_mult*dt;
      if(sp.move==='erratic')p.heading=wrap(p.heading+(random(s)-.5)*.35);
      if(sp.move==='hop')step*=now%900<200?2.2:0;
      // Grazing: a bug that is not fleeing stops to feed for a moment, then
      // moves on. This is what makes the tank read as alive while the spider is
      // asleep. A graze is cancelled the moment the spider gets close, because
      // the flee branch above takes over. Mirrors the PREY_GRAZE_* block in
      // main/prey.c.
      const grazing=now<(p.graze_until??0);
      if(!grazing&&now>=(p.next_graze_at??0)){
        const span=C.PREY_GRAZE_MIN_MS+Math.floor(random(s)*(C.PREY_GRAZE_MAX_MS-C.PREY_GRAZE_MIN_MS+1));
        p.graze_until=now+span;
        p.next_graze_at=now+span+C.PREY_GRAZE_GAP_MIN_MS+
          Math.floor(random(s)*(C.PREY_GRAZE_GAP_MAX_MS-C.PREY_GRAZE_GAP_MIN_MS+1));
      }
      if(now<(p.graze_until??0))step=0;
    }
    p.x+=Math.cos(p.heading)*step;p.y+=Math.sin(p.heading)*step;
    if(!p.fleeing&&sp.move==='flutter'&&now>=(p.graze_until??0)){const lateral=Math.sin(now*.006)*14*dt;p.x-=Math.sin(p.heading)*lateral;p.y+=Math.cos(p.heading)*lateral;}
    if(p.x<=34||p.x>=446){p.heading=wrap(PI-p.heading);p.wander_dir=p.heading;}
    if(p.y<=34||p.y>=446){p.heading=wrap(-p.heading);p.wander_dir=p.heading;}
    p.x=clamp(p.x,34,446);p.y=clamp(p.y,34,446);
    if(['fly','moth','fruitfly'].includes(sp.key)){
      if(now-p.frame_at>=(p.fleeing?70:140)){p.frame_at=now;p.frame=(p.frame+1)&1;}
    }else{
      const distance=Math.hypot(p.x-oldX,p.y-oldY);
      p.gait_distance=distance>.01?((p.gait_distance??0)+distance)%12:0;
      p.frame=p.gait_distance>=6?1:0;
    }
  }
}
function syncView(s) {
  const r=runtime(s),v=s.spider,walk=[1,2,9].includes(s.state),distance=Math.hypot(r.px-r.gait_x,r.py-r.gait_y);
  if(r.gait_valid&&walk&&distance>0&&distance<=12){v.gait_phase=(v.gait_phase+distance*TAU/24)%TAU;r.gait_moved_at=r.now_ms;}
  r.gait_x=r.px;r.gait_y=r.py;r.gait_valid=true;
  Object.assign(v,{x:r.px,y:r.py,heading:s.spider_heading,angle:s.spider_heading,state:s.state,
    walking:walk&&!!r.gait_moved_at&&r.now_ms-r.gait_moved_at<100,
    pounce:r.pounce?{...r.pounce,progress:clamp((r.now_ms-r.pounce.start)/r.pounce.duration,0,1)}:null,
    gift_visiting:r.gift_visiting,gift_interaction:r.gift_hold_until?GIFTS[s.gift.kind]?.interaction:null});
}
function resetLifeRuntime(s) {
  const now=s._engine.now_ms;
  s._engine=engineDefaults(now);s._engine.px=s.spider_x;s._engine.py=s.spider_y;
  Object.assign(s.spider,{gait_phase:0,walking:false});
  syncView(s);
}
export function lifeRetire(s, confirmed=false) {
  const options=services.get(s)??{};
  if(!retireLifePet(s,confirmed,freshState(options)))return false;
  resetLifeRuntime(s);return true;
}
export function lifeResume(s, index, confirmed=false) {
  if(!resumeLifePet(s,index,confirmed))return false;
  resetLifeRuntime(s);return true;
}
export function tick(s,seconds=.05) {
  if(!Number.isFinite(seconds)||seconds<0)throw new Error('Invalid tick duration');
  if(s.paused)return s;
  companionEnsure(s);
  const r=runtime(s);r.remainder+=seconds*1000;
  while(r.remainder+1e-8>=50){
    r.remainder-=50;r.now_ms+=50;
    lifeTick(s,.05);
    mindTick(s);needsTick(s);behaviourTick(s,.05);automationTick(s);preyTick(s,.05);
    if(s.bond>=75&&!s.gift.used){
      if(!r.next_bond_gift)r.next_bond_gift=r.now_ms+120000+roll(s,120000);
      else if(r.now_ms>=r.next_bond_gift){r.next_bond_gift=r.now_ms+120000+roll(s,120000);addSilk(s,2);}
    }else if(s.bond<75)r.next_bond_gift=0;
    s.poop=s.poop.filter(p=>p.expires>r.now_ms);syncView(s);
  }
  companionEnsure(s);
  const context=lifeContext(s,companionNow(s));
  s.activity=context.activity;s.expression=context.expression;
  return s;
}
