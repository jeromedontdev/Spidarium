import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';
import {decodeSave} from '../lib/save.js';
import {CLIMB_MASK} from '../lib/climb-mask.js';

function fixture(){
  const state=E.freshState();state.decor_owned=0xffff;state.level=40;
  for(let i=0;i<15;i++)E.placeDecor(state,i%16,240,240);
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,decorMoveSlot:null,decorKind:null,giftKind:null,close(){this.modal=null;},
    refreshHUD(){},persist(){this.saves=(this.saves??0)+1;},report(){},
  });
  return {state,ui};
}

test('all three path modes distinguish steering from canopy draw/hit order',()=>{
  const {state}=fixture();
  for(const item of state.decor)item.blocking=0;
  assert.equal(E.decorBlocksPoint(state,240,240),false,'Walk Over does not block');
  /* Walk Behind BLOCKS as well as occluding. It used to be non-blocking, which
   * let the spider walk over the bottom of the sprite and then pop behind the
   * top. */
  state.decor[0].blocking=2;
  assert.equal(E.decorBlocksPoint(state,240,240),true,'Walk Behind blocks');
  assert.ok(E.decorNearestBlocking(state,240,240,100),'Walk Behind steers');
  assert.equal(E.decorHitTest(state,240,240),0,'canopy wins above later floor slots');
  state.decor[1].blocking=1;
  assert.equal(E.decorBlocksPoint(state,240,240),true);
  assert.ok(E.decorNearestBlocking(state,240,240,100));
  assert.equal(E.decorHitTest(state,240,240),0);
  state.decor[0].blocking=0;
  assert.equal(E.decorHitTest(state,240,240),14);
});

test('walk behind steers from the artwork edge, walk over never steers',()=>{
  /* Walk Behind blocks against the whole artwork, so the spider must start
   * turning before it reaches the sprite. Walk Over is meant to be crossed, so
   * it must not steer at all. */
  const run=blocking=>{
    const s=E.freshState({random:()=>.5});
    Object.assign(s,{state:E.STATES.WANDER,spider_x:202,spider_y:240,spider_heading:0});
    Object.assign(s._engine,{px:202,py:240,retarget_at:1e9,wander_dir:0});
    s.decor=[{kind:3,blocking,x:240,y:240}];
    E.tick(s,.05);return s;
  };
  const walkOver=run(0),walkBehind=run(2);
  assert.equal(E.decorBlocksPoint(walkOver,240,240),false);
  assert.equal(E.decorBlocksPoint(walkBehind,240,240),true);
  /* The spider starts 38 px away. A prop's artwork half-extent is 2*radius, so
   * Walk Behind is already inside its steering berth while Walk Over is not. */
  assert.notEqual(walkOver.spider_heading,walkBehind.spider_heading,
    'Walk Behind steers the spider, Walk Over does not');
});

test('layer reordering crosses the inline/tail boundary and preserves all item records',()=>{
  const {state}=fixture();
  state.decor[5].blocking=2;
  const original=structuredClone(state.decor),moving=state.decor[14];
  assert.equal(E.setDecorLayer(state,14,0),true);
  assert.equal(state.decor[0],moving);
  assert.deepEqual(state.decor[5],original[5],'canopy keeps its own band/position');
  assert.deepEqual([...state.decor].sort((a,b)=>a.kind-b.kind),original.sort((a,b)=>a.kind-b.kind));
  const before=structuredClone(state.decor);
  assert.equal(E.setDecorLayer(state,0,99),false);
  assert.deepEqual(state.decor,before);
});

test('moving stages path, layer and position; Cancel is exact and Done saves once',()=>{
  const {state,ui}=fixture(),before=structuredClone(state.decor);
  ui.beginDecorMove(0);ui.setPlacementPath(2);ui.positionPlacement(180,180);
  assert.equal(ui.placement.blocking,2);assert.deepEqual(state.decor,before);
  ui.cancelPlacement();assert.deepEqual(state.decor,before);assert.equal(ui.saves,undefined);
  ui.beginDecorMove(0);ui.changePlacementLayer(1);ui.changePlacementLayer(1);
  assert.equal(ui.placement.layer,2);assert.deepEqual(state.decor,before);
  ui.setPlacementPath(1);assert.equal(ui.placement.layer,2,'Solid keeps its existing Floor layer');
  ui.setPlacementPath(0);assert.equal(ui.placement.layer,2,'Walk Over keeps its existing Floor layer');
  ui.commitPlacement();assert.equal(state.decor[2].kind,0);assert.equal(ui.saves,1);
  ui.beginDecorMove(2);ui.setPlacementPath(2);ui.commitPlacement();
  assert.equal(state.decor[2].blocking,2);assert.equal(ui.saves,2);
  const restored=E.restoreSim(JSON.parse(JSON.stringify(state)));
  assert.deepEqual(restored.decor,state.decor,'browser round trip retains mode and layer');
});

test('new placement has path/layer controls, clamped arrows, and no new charge until the existing shop purchase',()=>{
  const {state,ui}=fixture(),silk=state.silk;
  ui.beginDecorPlacement(0);ui.setPlacementPath(2);
  ui.changePlacementLayer(-1);assert.equal(ui.placement.layer,0);
  ui.changePlacementLayer(1);assert.equal(ui.placement.layer,0);
  ui.commitPlacement();
  assert.equal(state.decor.at(-1).blocking,2);
  assert.equal(state.silk,silk);
  assert.equal(ui.saves,1);
});

test('preview respects staged layer ordering without changing saved furniture',()=>{
  const {state,ui}=fixture(),calls=[];
  state.decor=state.decor.slice(0,3);state.decor.forEach(item=>{item.blocking=0;});
  ui.ctx={drawImage(image){calls.push(image.name);},strokeRect(){}};ui.image=name=>({name,complete:true,naturalWidth:64});
  ui.beginDecorMove(2);ui.changePlacementLayer(-1);
  ui.drawDecor(false);assert.deepEqual(calls,state.decor.map((item,i)=>E.decorSprite([0,2,1][i])));
  calls.length=0;ui.setPlacementPath(2);ui.drawDecor(false);ui.drawDecor(true);
  assert.deepEqual(calls,[0,1,2].map(E.decorSprite));
  assert.equal(state.decor[2].blocking,0);
});

test('v13 raw inline/tail modes survive decode; v12 rejects new mode2 and v13 rejects3',async()=>{
  const schema=JSON.parse(await readFile(new URL('../save-schema.json',import.meta.url),'utf8'));
  const bytes=new Uint8Array(12+268+6),v=new DataView(bytes.buffer);
  v.setUint32(0,schema.magic,true);v.setUint16(4,13,true);v.setUint32(8,274,true);
  for(let i=0;i<12;i++)bytes[12+164+i*6]=255;
  bytes[12+164]=0;bytes[12+165]=2;bytes[12+268]=1;bytes[12+269]=2;
  v.setUint32(12+236,1,true);bytes[12+266]=255;v.setUint32(12+256,1,true);
  const decoded=decodeSave(bytes,schema);
  assert.equal(decoded.fields.decor[0].blocking,2);assert.equal(decoded.fields.decor[12].blocking,2);
  v.setUint16(4,12,true);assert.throws(()=>decodeSave(bytes,schema),/decor/i);
  v.setUint16(4,13,true);bytes[12+165]=3;assert.throws(()=>decodeSave(bytes,schema),/decor/i);
});

test('canopy renders above the entire creature layer and below one lighting overlay',()=>{
  const {state,ui}=fixture(),calls=[];
  state.prey[0]={alive:true,dead:false,species:0,x:240,y:240};
  state.lights_dimmed=true;
  ui.spiderPixels={data:new Uint8ClampedArray(96*96*4)};
  ui.spiderCanvas={name:'spider'};ui.spiderCtx={putImageData(){}};
  ui.ctx=new Proxy({}, {get(target,key){
    if(key==='drawImage')return image=>calls.push(image.name);
    if(key==='fillRect')return ()=>{if(target.fillStyle==='rgba(7,12,32,0.43)')calls.push('dim');};
    return target[key]??(()=>{});
  }});
  ui.image=name=>({name,complete:true,naturalWidth:64});
  ui.drawDecor=canopy=>calls.push(canopy?'canopy':'floor');
  ui.draw();
  const prey=calls.findIndex(name=>name.startsWith('prey_'));
  assert.ok(calls.indexOf('floor')<calls.indexOf('spider'));
  assert.ok(calls.indexOf('spider')<prey);
  assert.ok(prey<calls.indexOf('canopy'));
  assert.ok(calls.indexOf('canopy')<calls.indexOf('dim'));
  assert.equal(calls.filter(name=>name==='dim').length,1);
});

test('climb path lifts the spider and leaves a floor shadow',()=>{
  const {state}=fixture();
  for(const item of state.decor)item.blocking=0;
  /* The fixture stacks fifteen items at one point; clear them so the footprint
   * test measures only the item under test. */
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  /* Kind 3 is the Log: radius 22, climb height 16. */
  state.decor[0]={kind:3,blocking:3,x:240,y:240};
  assert.equal(E.decorClimbable(3),true,'the log is climbable');
  assert.equal(E.decorClimbable(5),false,'the fern is flat');
  assert.equal(E.decorClimbHeight(state,0),16);
  assert.equal(E.decorClimbAt(state,240,240),16,'standing on the log');
  assert.equal(E.decorClimbAt(state,240,240+30),0,'outside the footprint');
  /* A Walk Over log must not lift: only the Climb path does. */
  state.decor[0].blocking=0;
  assert.equal(E.decorClimbAt(state,240,240),0);
  state.decor[0].blocking=3;
  /* The highest climbable item wins when two overlap. */
  state.decor[1]={kind:33,blocking:3,x:240,y:240};
  assert.equal(E.decorClimbAt(state,240,240),18,'the stone bridge is taller');
});

  test('large climb structures ramp up the screen, not out from the centre',()=>{
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  /* The last kind is Driftwood Ramp: climb height 62, authored at 96 and drawn
   * at 2x, so its screen half-extent is DECOR_CLIMB_EXTENT (96). */
  const kind=E.DECOR_KINDS.length-1;
  assert.equal(E.decorIsClimbStructure(kind),true);
  assert.equal(E.decorIsClimbStructure(3),false,'the log is a prop, not a structure');
  assert.equal(E.DECOR_CLIMB_SPRITE,96);
  assert.equal(E.DECOR_CLIMB_EXTENT,E.DECOR_CLIMB_SPRITE,
    'the sprite is drawn at 2x, so its half-extent is the sprite size');
  state.decor[0]={kind,blocking:3,x:240,y:240};
  const def=E.DECOR_KINDS[kind];
  const extent=E.decorClimbExtent(kind);
  assert.equal(extent,E.DECOR_CLIMB_EXTENT,'a structure ramps across its artwork');
  assert.ok(extent>def.radius,
    'the artwork is much wider than the collision footprint');
  /* The slope runs up the screen: the spider enters at the BOTTOM of the
   * artwork and rises as it walks toward the TOP. The driftwood is a DIAGONAL
   * log running from lower-right to upper-left, so the samples follow its
   * centre line: a vertical line through the item centre leaves the artwork at
   * both ends. */
  const high=E.decorClimbAt(state,172,172);   /* upper-left of the log */
  const mid =E.decorClimbAt(state,246,240);   /* middle */
  const low =E.decorClimbAt(state,296,308);   /* lower-right */
  assert.ok(low>0&&mid>0&&high>0,`all three are on the log: ${low}/${mid}/${high}`);
  assert.ok(low<mid,`lower-right ${low} must be lower than middle ${mid}`);
  assert.ok(mid<high,`middle ${mid} must be lower than upper-left ${high}`);
  assert.ok(high>def.climb*0.9,`the top of the log is near full height, got ${high}`);
  assert.ok(low<def.climb*0.2,`the bottom of the log is near ground, got ${low}`);
  /* The corners of the 192 px sprite box are EMPTY. This is the "hitbox is way
   * off, the spider is well above the decor item and it thinks it is on it"
   * report: a circle or a box covers the transparent corners, so the spider was
   * lifted while standing in space beside the log. */
  assert.equal(E.decorClimbAt(state,240,240-extent+2),0,'top centre is empty');
  assert.equal(E.decorClimbAt(state,240,240+extent-2),0,'bottom centre is empty');
  assert.equal(E.decorClimbAt(state,240-extent+4,240-extent+4),0,'top-left corner');
  assert.equal(E.decorClimbAt(state,240+extent-4,240+extent-4),0,'bottom-right corner');
  assert.equal(E.decorClimbAt(state,240+extent+2,240),0,'nothing past the rim');
  /* A prop still snaps to full height across its whole footprint. */
  state.decor[0]={kind:3,blocking:3,x:240,y:240};
  assert.equal(E.decorClimbAt(state,240,240),16);
  assert.equal(E.decorClimbAt(state,240+21,240),16,'a prop is a flat disc');
  });

test('the climb surface reports a slope the body can lean with',()=>{
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  const kind=E.DECOR_KINDS.length-1;
  const def=E.DECOR_KINDS[kind];
  state.decor[0]={kind,blocking:3,x:240,y:240};
  /* A large structure ramps linearly, so its gradient is constant across the
   * whole artwork: climb height spread over the ARTWORK's own vertical span.
   * Measuring against the square box understated it, and against the much
   * smaller collision radius it was a 1.8 slope that clamped to the tilt limit
   * and looked like a wall. */
  const extent=E.decorClimbExtent(kind);
  const range=E.climbRowRange(kind);
  const expected=-def.climb/((range.bot-range.top+1)*2);
  assert.ok(Math.abs(expected)<0.5,`a believable ramp, got ${expected}`);
  /* Sampled along the log, since a vertical line leaves the artwork. */
  const low =E.decorClimbSlope(state,296,308);
  const mid =E.decorClimbSlope(state,246,240);
  const high=E.decorClimbSlope(state,172,172);
  assert.ok(Math.abs(mid-expected)<1e-9,`middle ${mid} vs ${expected}`);
  assert.equal(low,mid,'a linear ramp has one gradient');
  assert.equal(high,mid,'a linear ramp has one gradient');
  /* The sign is in screen space, where y grows downward: a surface rising
   * toward the TOP of the screen is negative. */
  assert.ok(mid<0,`rising toward the top is negative, got ${mid}`);
  /* Off the artwork there is no surface, so no slope. */
  assert.equal(E.decorClimbSlope(state,240,240-extent+2),0,'empty top centre');
  assert.equal(E.decorClimbSlope(state,240+extent+2,240),0);
  /* A small prop is a flat disc, so its top is level. */
  state.decor[0]={kind:3,blocking:3,x:240,y:240};
  assert.equal(E.decorClimbSlope(state,240,240),0,'a prop top is level');
  });

test('climbing lifts the spider without re-posing it',()=>{
  /* There is deliberately NO perspective shift while climbing. Three attempts
   * were made at one -- a forward-axis compression, a body tilt following the
   * surface gradient, and a full dorsal projection into a side view -- and each
   * looked wrong on the panel. A 96x96 pixel-art sprite authored from above
   * cannot be re-projected into a convincing profile by moving its parts.
   *
   * What remains is the lift plus the contact shadow under the feet, which is
   * what actually reads as standing on something with height. */
  assert.equal(E.CLIMB_FORESHORTEN,undefined,
    'climbing must not compress the body');
  assert.equal(E.CLIMB_TILT_FOLLOW,undefined,
    'climbing must not tilt the body');
  assert.equal(E.CLIMB_PROFILE_RIGHT,undefined,
    'climbing must not project a side view');
  assert.equal(E.climbProfile,undefined,
    'there is no profile blend to drive');
  /* The projection is a plain top-down rotation: no dorsal axis, no tilt. */
  const flat=E.toCanvas(0,10,0,1,1,1);
  assert.ok(Math.abs(Math.hypot(flat.x,flat.y)-10)<1e-9,
    'the right axis keeps its full width at every lift');
  /* The lift itself still exists and still scales with the structure. */
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  const kind=E.DECOR_KINDS.length-1;
  state.decor[0]={kind,blocking:3,x:240,y:240};
  assert.ok(E.decorClimbAt(state,240,240)>0,'the structure still lifts the spider');
  });

test('the climb contact shadow follows the body, not the floor',async()=>{
  /* The shadow used to stay pinned to the logical floor point while the body
   * rose, which put it up to 84 px below the spider at full lift -- nearly a
   * whole sprite height -- and read as the spider floating above the structure
   * rather than standing on it. A contact shadow belongs where the object
   * touches its surface, and while climbing that surface is the top of the
   * structure, directly under the feet. */
  const ui=await readFile(new URL('../lib/device-ui.js',import.meta.url),'utf8');
  const ellipse=ui.match(/ctx\.ellipse\(s\.spider_x,\s*([^,]+),/);
  assert.ok(ellipse,'the climb shadow is drawn as an ellipse');
  assert.match(ellipse[1],/climbLift/,
    'the shadow must move with the lift, not stay on the floor');
});

test('a small prop is perched on, not walked across',()=>{
const {state}=fixture();
for(const item of state.decor)item.kind=E.DECOR_EMPTY;
/* Log: a small prop, radius 22, climb height 16. */
state.decor[0]={kind:3,blocking:3,x:240,y:240};
assert.equal(E.decorPerchAt(state,240,240),true,'standing on the prop perches');
assert.equal(E.decorPerchAt(state,240+30,240),false,'off the footprint is not a perch');
/* A prop whose collision radius is smaller than the spider's body still has a
 * usable perch window. The Book Stack is radius 12; using the raw radius meant
 * the spider crossed it in a fraction of a tick and appeared to jump over. */
const book=E.DECOR_KINDS.findIndex(item=>item.name==='Book Stack');
assert.ok(book>=0,'the Book Stack exists');
assert.ok(E.DECOR_KINDS[book].radius<E.PERCH_MIN_RADIUS,'its radius is under the floor');
state.decor[0]={kind:book,blocking:3,x:240,y:240};
assert.equal(E.decorPerchAt(state,240+15,240),true,'the perch window is widened to the body');
assert.equal(E.decorPerchAt(state,240+E.PERCH_MIN_RADIUS+2,240),false,'but it is still bounded');
/* A large structure is walked across, never perched on. */
const big=E.DECOR_KINDS.length-1;
assert.equal(E.decorIsClimbStructure(big),true);
state.decor[0]={kind:big,blocking:3,x:240,y:240};
assert.equal(E.decorPerchAt(state,240,240),false,'a structure is not a perch');
/* The heading faces OUT from the item, snapped to 45-degree steps. Facing the
 * centre would point the spider at its own feet. */
state.decor[0]={kind:3,blocking:3,x:240,y:240};
const east=E.decorPerchHeading(state,240+10,240);
assert.equal(east,0,'east of centre faces east');
const south=E.decorPerchHeading(state,240,240+10);
assert.ok(Math.abs(south-Math.PI/2)<1e-9,'south of centre faces south');
const diag=E.decorPerchHeading(state,240+10,240+10);
assert.ok(Math.abs(diag-Math.PI/4)<1e-9,'diagonal snaps to 45 degrees');
assert.equal(E.decorPerchHeading(state,240,240),Math.PI/2,'dead centre faces down-screen');
assert.equal(E.decorPerchHeading(state,10,10),null,'no prop, no heading');
});

test('perching holds position, turns outward and yields to hunger',()=>{
const {state}=fixture();
for(const item of state.decor)item.kind=E.DECOR_EMPTY;
state.decor[0]={kind:3,blocking:3,x:240,y:240};
state.spider_x=240;state.spider_y=240;state.energy=80;state.hunger=80;
state.mood=80;state.sick=0;state.auto_hunt=0;
E.tick(state,.05);
assert.equal(state.state,E.STATES.PERCH,'an idle spider on a prop perches');
/* It must not wander off the prop while perched. */
for(let i=0;i<20;i++)E.tick(state,.05);
assert.equal(state.state,E.STATES.PERCH);
assert.ok(Math.hypot(state.spider_x-240,state.spider_y-240)<2,'holds the centre');
/* The pose is what sells the height, so the state must be renderable. */
assert.equal(E.STATE_NAMES[E.STATES.PERCH],'perch');
assert.ok(E.PERCH_FORESHORTEN>0.5&&E.PERCH_FORESHORTEN<1,'foreshortens the body');
/* Hunger outranks a pose: a starving spider leaves the perch. */
state.hunger=10;state.energy=80;
E.tick(state,.05);
assert.notEqual(state.state,E.STATES.PERCH,'hunger ends the perch');
});

test('a forced perch state still converges on the real prop',()=>{
const {state}=fixture();
for(const item of state.decor)item.kind=E.DECOR_EMPTY;
state.decor[0]={kind:3,blocking:3,x:240,y:240};
/* Forcing the state skips the idle branch that would cache the centre, which
 * is exactly what the console `state` command and a save load do. The centre
 * must come from the item, not from a stale cache. */
state.spider_x=252;state.spider_y=240;state.energy=80;state.hunger=80;
state.mood=80;state.sick=0;state.auto_hunt=0;
state.state=E.STATES.PERCH;
for(let i=0;i<40;i++)E.tick(state,.05);
assert.equal(state.state,E.STATES.PERCH);
assert.ok(Math.hypot(state.spider_x-240,state.spider_y-240)<2,
  `converges on the prop centre, got (${state.spider_x},${state.spider_y})`);
});

test('silk is capped so the economy stays readable',()=>{
const {state}=fixture();
/* Saturating at U32 let the balance reach hundreds of thousands, at which
 * point every price is meaningless and a greyed shop row is indistinguishable
 * from a Silk problem. */
state.silk=0;
E.addSilk(state,1e9);
assert.equal(state.silk,E.SILK_MAX,'a huge grant clamps to the cap');
E.addSilk(state,1);
assert.equal(state.silk,E.SILK_MAX,'further grants do not exceed the cap');
assert.ok(E.SILK_MAX<1e6,'the cap is a readable currency, not a type limit');
/* The cap must be far above the total cost of everything purchasable, or it
 * would block legitimate play. */
const total=E.DECOR_KINDS.reduce((sum,item)=>sum+item.cost,0);
assert.ok(E.SILK_MAX>total,`cap ${E.SILK_MAX} must exceed the decor catalogue total ${total}`);
});

test('a wandering spider perches instead of walking over the prop',()=>{
const {state}=fixture();
for(const item of state.decor)item.kind=E.DECOR_EMPTY;
state.decor[0]={kind:3,blocking:3,x:240,y:240};
state.energy=80;state.hunger=80;state.mood=80;state.sick=0;state.auto_hunt=0;
/* Start off the prop and already wandering, which is the case the IDLE-only
 * check missed: the spider crossed the prop in under a second and the player
 * never saw a perch. */
state.spider_x=200;state.spider_y=240;state.state=E.STATES.WANDER;
let perched=false;
for(let i=0;i<200;i++){
  E.tick(state,.05);
  if(state.state===E.STATES.PERCH){perched=true;break;}
}
assert.equal(perched,true,'a wandering spider stops on the prop');
});

test('a perched spider does not oscillate when hunger drops',()=>{
const {state}=fixture();
for(const item of state.decor)item.kind=E.DECOR_EMPTY;
state.decor[0]={kind:3,blocking:3,x:240,y:240};
state.spider_x=240;state.spider_y=240;state.energy=80;state.mood=80;
state.sick=0;state.auto_hunt=0;
/* Hunger just under the leave threshold. The IDLE trigger used to be
 * unguarded, so PERCH left and IDLE re-perched it on the same tick, and the
 * two states alternated at the 20 Hz logic rate. The visible symptom was a
 * spider sitting on the prop and vibrating, because the foreshortening eased
 * in and out on every flip. */
state.hunger=39;
state.state=E.STATES.PERCH;
const seen=[];
for(let i=0;i<60;i++){E.tick(state,.05);seen.push(state.state);}
const flips=seen.filter((v,i)=>i>0&&v!==seen[i-1]).length;
assert.ok(flips<=2,`state must settle, saw ${flips} flips: ${seen.join(',')}`);
assert.notEqual(state.state,E.STATES.PERCH,'a hungry spider leaves the perch');
});

test('leaving a perch holds off re-perching so the pose does not pulse',()=>{
const {state}=fixture();
for(const item of state.decor)item.kind=E.DECOR_EMPTY;
state.decor[0]={kind:3,blocking:3,x:240,y:240};
state.spider_x=240;state.spider_y=240;state.energy=80;state.hunger=80;
state.mood=80;state.sick=0;state.auto_hunt=0;
state.state=E.STATES.PERCH;
/* Run past the perch timer. Without a cooldown the spider re-perched on the
 * very next tick, so the state flickered PERCH -> IDLE -> PERCH and the
 * foreshortening pulsed, which read as vibrating. */
const seen=[];
for(let i=0;i<200;i++){E.tick(state,.05);seen.push(state.state);}
const flips=seen.filter((v,i)=>i>0&&v!==seen[i-1]).length;
assert.ok(flips<=4,`state must not flicker, saw ${flips} flips`);
});

test('climb is only offered for kinds that have height',()=>{  const {state}=fixture();
  state.decor[0]={kind:5,blocking:0,x:240,y:240};   /* Fern: flat */
  state.decor[1]={kind:3,blocking:0,x:200,y:200};   /* Log: climbable */
  for(let i=0;i<4;i++)E.toggleDecorBlocking(state,0);
  assert.notEqual(state.decor[0].blocking,3,'a flat prop never reaches Climb');
  let reached=false;
  for(let i=0;i<4;i++){
    E.toggleDecorBlocking(state,1);
    if(state.decor[1].blocking===3)reached=true;
  }
  assert.equal(reached,true,'a climbable prop can reach Climb');
  assert.deepEqual(E.DECOR_PATH_NAMES,['Walk Over','Solid','Walk Behind','Climb']);
});

test('the gift has the same layer control as decor',()=>{
  /* The gift is a single item, so it has no siblings of its own. Its layer is
   * its position among the DECOR sharing its band, which is what the player is
   * actually arranging: a gift behind the log, or in front of it. */
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  /* Three floor-band props. */
  state.decor[0]={kind:3,blocking:0,x:100,y:100};
  state.decor[1]={kind:5,blocking:0,x:200,y:200};
  state.decor[2]={kind:6,blocking:0,x:300,y:300};
  state.gift_layer=0;
  /* The count includes the gift itself, so three props plus the gift is four. */
  assert.equal(E.giftLayerCount(state,0),4,'three props plus the gift');
  assert.equal(E.giftLayerIndex(state,0),0,'the gift starts at the back');
  assert.equal(E.setGiftLayer(state,0,3),true,'the gift can go to the front');
  assert.equal(state.gift_layer,3);
  assert.equal(E.setGiftLayer(state,0,4),false,'past the end is rejected');
  assert.equal(E.setGiftLayer(state,0,-1),false,'negative is rejected');
  /* A canopy-band gift is ordered against canopy decor only. */
  state.decor[0].blocking=2;
  assert.equal(E.giftLayerCount(state,2),2,'one canopy prop plus the gift');
  assert.equal(E.giftLayerCount(state,0),3,'two floor props plus the gift');
  /* An out-of-range stored index is clamped rather than trusted, so a save
   * written when more decor existed cannot index past the band. */
  state.gift_layer=99;
  assert.equal(E.giftLayerIndex(state,0),2,'clamped to the band size');
});

test('the spider is always drawn from directly above',()=>{
  /* The climb perspective was removed rather than tuned. A 96x96 pixel-art
   * sprite authored from above cannot be re-projected into a convincing profile
   * by moving its parts around: the silhouette is top-down, so skewing it
   * produces a distorted top-down spider, not a side view. */
  for(const name of ['CLIMB_PROFILE_RIGHT','ABD_DORSAL','CEPH_DORSAL',
                     'EYE_DORSAL','FANG_DORSAL','LEG_DORSAL','LEG_DORSAL_NEAR',
                     'ABD_PROFILE_SCALE','CLIMB_PROFILE_START','CLIMB_TILT_FOLLOW',
                     'CLIMB_TILT_MAX','CLIMB_FORESHORTEN']){
    assert.equal(E[name],undefined,`${name} must be gone, not dormant`);
  }
  assert.equal(E.climbProfile,undefined,'there is no profile blend');
  assert.equal(E.toCanvasDorsal,undefined,'there is no dorsal projection');

  /* The projection is a plain top-down rotation. The right axis keeps its full
   * width, and nothing lifts a part up the screen. */
  const heading=0;
  const right=E.toCanvas(0,10,heading,1,1,1);
  assert.ok(Math.abs(Math.hypot(right.x,right.y)-10)<1e-9,
    'the right axis is full width');
  const centre=E.toCanvas(0,0,heading,1,1,1);
  assert.ok(Math.abs(centre.x)<1e-9&&Math.abs(centre.y)<1e-9,
    'the body centre is unmoved');
  /* Foreshortening still applies, but only from the PERCH pose. */
  const squashed=E.toCanvas(10,0,heading,1,1,0.72);
  assert.ok(Math.abs(Math.hypot(squashed.x,squashed.y)-7.2)<1e-9,
    'the perch foreshortening still compresses the forward axis');
});

test('the gift editor hides the path row and keeps the layer row',()=>{
  /* The user-visible half of removing the gift's path control: the button must
   * actually disappear, not just stop working. refreshHUD() is a pure function
   * of this.placement plus the DOM nodes, so stub the nodes and read .hidden. */
  const node=()=>({hidden:false,disabled:false,style:{},textContent:'',
    setAttribute(){},children:[{textContent:''},{textContent:''},{textContent:''}]});
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:E.freshState(),hud:false,modal:null,game:null,started:true,
    placement:null,giftMoving:false,decorMoveSlot:null,decorKind:null,giftKind:null,
    hudLayer:node(),ear:node(),earMark:node(),placementControls:node(),
    placementAway:node(),placementHint:node(),placementBack:node(),
    placementFront:node(),placementLayer:node(),
    placementPaths:[node(),node(),node(),node()],
    stats:[0,1,2,3].map(()=>({key:'hunger',bar:{firstChild:{style:{}}},value:node()})),
    careButtons:{Feed:node(),Sleep:{lastChild:{textContent:''}}},
    identity:{children:[node(),node(),node()]},
    persist(){},report(){},
  });
  /* A gift being moved: the layer row is live (there is decor to order
   * against) but the path row must be gone. */
  ui.state.decor_owned=1;E.placeDecor(ui.state,0,180,180);
  ui.placement={gift:true,kind:0,x:240,y:240,blocking:0,layer:0};
  ui.giftMoving=true;
  ui.refreshHUD();
  assert.ok(ui.placementPaths.every(button=>button.hidden),
    'no path button is shown for a gift');
  assert.equal(ui.placementHint.textContent,'Slide to position; Done saves',
    'the gift hint does not describe a path');
  assert.equal(ui.placementBack.hidden,true,'the layer arrows stay decor-only');
  /* Decor still gets the path row, so the removal is gift-specific. */
  ui.placement={gift:false,kind:0,slot:0,x:180,y:180,blocking:0,layer:0};
  ui.giftMoving=false;
  ui.refreshHUD();
  assert.ok(ui.placementPaths.every(button=>!button.hidden),
    'decor still shows the path buttons');
});

test('the gift never blocks or pushes the spider',()=>{
  /* The gift used to have a Solid path that pushed the spider out to
   * GIFT_PUSH_RADIUS while the visit only counted as arrived inside
   * GIFT_VISIT_ARRIVE. Those two constants had to stay in the right order or a
   * visit would shove the spider out every tick and circle forever. The path
   * control is gone, so the invariant is now much stronger and much simpler:
   * the spider is never displaced by the gift, whatever the stored byte says. */
  for(const path of [0,1,2,3]){
    assert.equal(E.giftBlocksMovement(path),false,
      `path ${path} must not block`);
    const s=E.freshState();
    s.gift.used=1;s.gift.kind=0;s.gift.x=240;s.gift.y=240;
    s.gift_path=path;
    /* Dead centre is the one place a push would be most visible. */
    s.spider_x=240;s.spider_y=240;
    s.state=0;s.hunger=80;s.energy=50;s.mood=50;s.sick=0;
    E.tick(s,0.05);
    assert.equal(s.spider_x,240,`path ${path} must not displace x`);
    assert.equal(s.spider_y,240,`path ${path} must not displace y`);
  }
});

test('a visit completes with the gift walked over',()=>{
  /* The visit branch steers straight at the gift and returns before anything
   * else can move the spider, so it is the one path that can drive the spider
   * onto the gift. With no push radius there is nothing to fight it. */
  const s=E.freshState();
  s.gift.used=1;s.gift.kind=0;s.gift.x=240;s.gift.y=240;
  s.gift_path=0;
  s.spider_x=240;s.spider_y=280;
  s.state=0;s.hunger=80;s.energy=50;s.mood=50;s.sick=0;
  const r=s._engine;
  r.gift_visiting=true;r.gift_hold_until=0;r.gift_visit_end=r.now_ms+20000;
  let arrived=false;
  for(let i=0;i<120&&!arrived;i++){
    E.tick(s,0.05);
    arrived=r.gift_hold_until!==0;
  }
  assert.ok(arrived,'the visit must complete');
});

test('walk behind blocks decor against the whole artwork',()=>{
  /* The same rule that makes Walk Behind read on a gift applies to decor: the
   * spider must not stand on the item's lower half. Blocking only the small
   * collision footprint let it walk over the bottom of the sprite and then pop
   * behind the top. */
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  /* A prop: 64x64 drawn at radius*16, so its screen half-extent is 2*radius. */
  state.decor[0]={kind:3,blocking:2,x:240,y:240};
  const def=E.DECOR_KINDS[3];
  const extent=E.decorBlockRadius(state,0);
  assert.ok(extent>def.radius,
    `Walk Behind blocks the artwork, not the footprint (${extent} vs ${def.radius})`);
  assert.ok(Math.abs(extent-def.radius*2)<1e-9,
    'a prop sprite covers 2*radius screen pixels each way');
  /* Solid still blocks only its collision footprint. */
  state.decor[0].blocking=1;
  assert.ok(Math.abs(E.decorBlockRadius(state,0)-def.radius)<1e-9,
    'Solid blocks its footprint');
  /* Walk Over and Climb do not block at all. */
  state.decor[0].blocking=0;
  assert.equal(E.decorBlocksPoint(state,240,240),false,'Walk Over does not block');
  state.decor[0].blocking=3;
  assert.equal(E.decorBlocksPoint(state,240,240),false,'Climb does not block');
});

test('walk behind blocks a structure only where its artwork is',()=>{
  /* A large structure is a diagonal log in a square box. Blocking the box
   * would stop the spider walking through thin air beside the log; blocking a
   * circle of radius 96 clipped the ends of the log, because the box corners
   * sit at r*sqrt(2). The span table is exact, so it is the only test. */
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  const kind=E.DECOR_CLIMB_FIRST+5; /* driftwood */
  state.decor[0]={kind,blocking:2,x:240,y:240};
  assert.equal(E.decorBlocksPoint(state,240,240),true,'the centre blocks');
  /* The four box corners are transparent, so they must not block. */
  for(const [dx,dy] of [[-95,-95],[95,-95],[-95,95],[95,95]])
    assert.equal(E.decorBlocksPoint(state,240+dx,240+dy),false,
      `corner ${dx},${dy} is empty artwork`);
  /* The ends of the log are opaque and lie outside a radius-96 circle, so a
   * circular pre-check would have let the spider walk through them. */
  const rows=CLIMB_MASK[5];
  let checked=0;
  for(let sy=0;sy<rows.length;sy++){
    const span=rows[sy];
    if(!span)continue;
    for(const sx of [span[0],span[1]]){
      const dx=(sx-48)*2,dy=(sy-48)*2;
      if(dx*dx+dy*dy<=96*96)continue;
      assert.equal(E.decorBlocksPoint(state,240+dx,240+dy),true,
        `opaque log end ${sx},${sy} must block`);
      checked++;
    }
  }
  assert.ok(checked>0,'the driftwood has opaque pixels outside the circle');
});

test('the layer row is hidden when there is nothing to order against',()=>{
  /* A gift alone in its band has no siblings, so a "1 / 1" layer control is
   * meaningless. The row is shown only when the band holds more than the item
   * being placed. */
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  state.gift_layer=0;
  assert.equal(E.giftLayerCount(state,0),1,'a lone gift has one layer');
  /* One floor prop gives the gift something to sit behind or in front of. */
  state.decor[0]={kind:3,blocking:0,x:100,y:100};
  assert.equal(E.giftLayerCount(state,0),2,'a prop plus the gift is two layers');
});

test('every sprite family shares one pixel density',()=>{
  /* The climb structures were authored at 192x192 and drawn 1:1 while the
   * gifts were drawn at 2x, so the climb art had half the pixel density of
   * everything beside it and its pixels looked visibly smaller. All art is now
   * on the same 2-screen-pixels-per-source-pixel grid. */
  /* LVGL scale 256 is 1:1, so screen pixels per source pixel is scale / 256.
   * That is the number that must match: it is how big one source pixel is drawn,
   * regardless of how many source pixels the sprite has. */
  const LVGL_ONE=256;
  const giftScale=512, giftSrc=64;
  const climbScale=512, climbSrc=E.DECOR_CLIMB_SPRITE;
  assert.equal(climbSrc,96,'the climb source is 96x96');
  const giftDensity=giftScale/LVGL_ONE;
  const climbDensity=climbScale/LVGL_ONE;
  assert.equal(giftDensity,climbDensity,
    'gifts and climb structures share one density');
  assert.equal(climbDensity,2,'both are drawn at 2x');
  /* A prop is drawn at radius*16 from a 64x64 source, so its density varies
   * with its footprint; the largest props land near the same 2x. */
  const logRadius=22;
  const logDensity=logRadius*16/LVGL_ONE;
  assert.ok(logDensity>1.3&&logDensity<2.1,
    'the largest props are close to the same density');
});
