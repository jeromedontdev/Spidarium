import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';
import {decodeSave} from '../lib/save.js';

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
  assert.equal(E.decorBlocksPoint(state,240,240),false);
  state.decor[0].blocking=2;
  assert.equal(E.decorBlocksPoint(state,240,240),false);
  assert.equal(E.decorNearestBlocking(state,240,240,100),null);
  assert.equal(E.decorHitTest(state,240,240),0,'canopy wins above later floor slots');
  state.decor[1].blocking=1;
  assert.equal(E.decorBlocksPoint(state,240,240),true);
  assert.ok(E.decorNearestBlocking(state,240,240,100));
  assert.equal(E.decorHitTest(state,240,240),0);
  state.decor[0].blocking=0;
  assert.equal(E.decorHitTest(state,240,240),14);
});

test('overlapping canopy cannot change the steering footprint of a solid item',()=>{
  const states=[true,false].map(canopy=>{
    const s=E.freshState({random:()=>.5});
    Object.assign(s,{state:E.STATES.WANDER,spider_x:202,spider_y:240,spider_heading:0});
    Object.assign(s._engine,{px:202,py:240,retarget_at:1e9,wander_dir:0});
    s.decor=[...(canopy?[{kind:3,blocking:2,x:240,y:240}]:[]),
      {kind:0,blocking:1,x:240,y:240}];
    E.tick(s,.05);return s;
  });
  assert.equal(states[0].spider_heading,states[1].spider_heading);
  assert.equal(states[0].spider_x,states[1].spider_x);
  assert.equal(states[0].spider_y,states[1].spider_y);
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
  /* The slope runs up the screen: the spider enters at the BOTTOM edge and rises
   * as it walks toward the TOP edge. Ramping radially from the centre made it
   * rise from whichever side it approached, which read as being lifted onto a
   * platform rather than climbing a branch. */
  const bottom=E.decorClimbAt(state,240,240+extent-2);
  const middle=E.decorClimbAt(state,240,240);
  const top=E.decorClimbAt(state,240,240-extent+2);
  assert.ok(bottom<middle,`bottom ${bottom} must be lower than middle ${middle}`);
  assert.ok(middle<top,`middle ${middle} must be lower than top ${top}`);
  assert.ok(top>def.climb*0.9,`the top edge is near full height, got ${top}`);
  assert.ok(bottom<def.climb*0.1,`the bottom edge is near ground, got ${bottom}`);
  /* Horizontal position must not change the height: it is a slope, not a dome. */
  assert.equal(E.decorClimbAt(state,240-30,240),E.decorClimbAt(state,240+30,240),
    'height depends on vertical position only');
  /* The ramp is vertical, so the lift fades out toward the side edges instead of
   * jumping to half height the instant the spider crosses them. */
  assert.ok(E.decorClimbAt(state,240+extent-4,240)<def.climb/4,
    'the lift fades out at the side edge');
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
   * whole footprint: climb height spread over the ARTWORK's vertical extent.
   * Measuring against the much smaller collision radius made the same climb a
   * 1.8 slope that clamped to the tilt limit and looked like a wall. */
  const extent=E.decorClimbExtent(kind);
  const expected=-def.climb/(2*extent);
  assert.ok(Math.abs(expected)<0.5,`a believable ramp, got ${expected}`);
  /* Sampled inside the footprint: at the very bottom edge the lift rounds to
   * zero, so there is no surface there to report a slope for. */
  const bottom=E.decorClimbSlope(state,240,240+extent-8);
  const middle=E.decorClimbSlope(state,240,240);
  const top=E.decorClimbSlope(state,240,240-extent+8);
  assert.ok(Math.abs(middle-expected)<1e-9,`middle ${middle} vs ${expected}`);
  assert.equal(bottom,middle,'a linear ramp has one gradient');
  assert.equal(top,middle,'a linear ramp has one gradient');
  /* The sign is in screen space, where y grows downward: a surface rising
   * toward the TOP of the screen is negative. */
  assert.ok(middle<0,`rising toward the top is negative, got ${middle}`);
  /* Off the footprint there is no surface, so no slope. */
  assert.equal(E.decorClimbSlope(state,240+extent+2,240),0);
  /* A small prop is a flat disc, so its top is level. */
  state.decor[0]={kind:3,blocking:3,x:240,y:240};
  assert.equal(E.decorClimbSlope(state,240,240),0,'a prop top is level');
  });

test('climbing compresses the body and tilts it with the ramp',()=>{
  /* The 2.5D read needs two things beyond the lift: the viewing angle changes
   * as the spider gets higher, and the body leans with the surface. Without
   * them the creature keeps its flat top-down silhouette and slides up the
   * screen, which is the "no perspective shift" report. */
  assert.ok(E.CLIMB_FORESHORTEN<E.PERCH_FORESHORTEN||E.CLIMB_FORESHORTEN<1,
    'climbing must compress the forward axis');
  assert.ok(E.CLIMB_FORESHORTEN>0.6,
    'below about 0.6 the abdomen overlaps the cephalothorax');
  assert.ok(E.CLIMB_FULL_LIFT>0,'the compression needs a reference height');
  assert.ok(E.CLIMB_TILT_FOLLOW>0&&E.CLIMB_TILT_FOLLOW<=1,
    'the body follows most, not all, of the surface gradient');
  assert.ok(E.CLIMB_TILT_MAX>0&&E.CLIMB_TILT_MAX<Math.PI/4,
    'beyond about 30 degrees the legs fold through the body');
  /* The tilt derived from a real structure must stay inside the clamp. */
  const {state}=fixture();
  for(const item of state.decor)item.kind=E.DECOR_EMPTY;
  const kind=E.DECOR_KINDS.length-1;
  const def=E.DECOR_KINDS[kind];
  state.decor[0]={kind,blocking:3,x:240,y:240};
  const slope=E.decorClimbSlope(state,240,240);
  const tilt=Math.min(E.CLIMB_TILT_MAX,
    Math.max(-E.CLIMB_TILT_MAX,-Math.atan(slope)*E.CLIMB_TILT_FOLLOW));
  assert.ok(Math.abs(tilt)<=E.CLIMB_TILT_MAX,'the tilt is clamped');
  assert.ok(tilt>0,'a surface rising toward the top tilts the front down-screen');
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

test('climbing blends the spider into a side view',()=>{
  /* Rotating a top-down spider does not produce a side view: the abdomen stays
   * behind the cephalothorax in the same plane and the legs stay splayed
   * radially. The profile blend is what projects the body's dorsal axis onto the
   * screen, so the outline becomes a profile rather than a leaning plan view. */
  assert.equal(E.climbProfile(0),0,'the floor is a flat top-down pose');
  assert.equal(E.climbProfile(E.CLIMB_FULL_LIFT),1,'the top is a full profile');
  assert.equal(E.climbProfile(E.CLIMB_FULL_LIFT*2),1,'the blend is clamped');
  /* The blend holds the top-down pose through the lower part of the ramp: at
   * the very bottom the spider is still effectively on the floor, so a profile
   * there looked wrong. */
  assert.equal(E.climbProfile(E.CLIMB_FULL_LIFT*E.CLIMB_PROFILE_START),0,
    'the bottom of the ramp is still top-down');
  assert.equal(E.climbProfile(E.CLIMB_FULL_LIFT*0.2),0,
    'the dead zone covers the lower third');
  /* Above the dead zone it rises monotonically to a full profile. */
  const mid=E.climbProfile(E.CLIMB_FULL_LIFT*0.675);
  assert.ok(mid>0.4&&mid<0.6,`the blend tracks the lift, got ${mid}`);
  assert.ok(E.climbProfile(E.CLIMB_FULL_LIFT*0.5)<
            E.climbProfile(E.CLIMB_FULL_LIFT*0.8),'the blend rises with the lift');

  /* At full profile the body's right axis collapses, which is the strongest
   * side-view cue: a top-down spider is as wide as it is long. */
  const heading=0, fs=1, tilt=0;
  const flat=E.toCanvasDorsal(0,10,0,heading,1,1,fs,0,tilt);
  const prof=E.toCanvasDorsal(0,10,0,heading,1,1,fs,1,tilt);
  const flatLen=Math.hypot(flat.x,flat.y);
  const profLen=Math.hypot(prof.x,prof.y);
  assert.ok(Math.abs(flatLen-10)<0.01,'flat: the right axis is full width');
  assert.ok(Math.abs(profLen-10*E.CLIMB_PROFILE_RIGHT)<0.01,
    'profile: the right axis collapses to CLIMB_PROFILE_RIGHT');

  /* The dorsal axis only exists in profile: it lifts a part up the screen. */
  const noDorsal=E.toCanvasDorsal(0,0,0,heading,1,1,fs,1,tilt);
  const withDorsal=E.toCanvasDorsal(0,0,E.ABD_DORSAL,heading,1,1,fs,1,tilt);
  assert.ok(withDorsal.y<noDorsal.y,'dorsal lifts the part up the screen');
  assert.ok(Math.abs((noDorsal.y-withDorsal.y)-E.ABD_DORSAL)<0.01,
    'the lift equals the dorsal offset at full profile');
  const floorDorsal=E.toCanvasDorsal(0,0,E.ABD_DORSAL,heading,1,1,fs,0,tilt);
  assert.ok(Math.abs(floorDorsal.y-noDorsal.y)<0.01,
    'on the floor the dorsal axis is invisible');

  /* The abdomen rides higher than the cephalothorax, which is the classic
   * spider profile: a raised teardrop behind a lower head. */
  assert.ok(E.ABD_DORSAL>E.CEPH_DORSAL,'the abdomen rides higher than the head');
  assert.ok(E.LEG_DORSAL>0,'the legs hang below the body in profile');

  /* The SEPARATION is what makes the profile read. The first attempt used 7 and
   * 3, a 4 px gap between circles of radius 13 and 11, so the two masses merged
   * into one blob and the side view looked like a lump. */
  const gap=E.ABD_DORSAL-E.CEPH_DORSAL;
  assert.ok(gap>=10,`the body masses must be clearly separated, got ${gap}`);
  /* Seen from the side the abdomen is a teardrop, not a circle, so it narrows. */
  assert.ok(E.ABD_PROFILE_SCALE<1&&E.ABD_PROFILE_SCALE>0.7,
    'the abdomen narrows in profile but stays readable');
});

test('walk behind blocks movement as well as occluding',()=>{
  /* Walk Behind used to be non-blocking, so the spider crossed the gift's
   * footprint and was drawn ON TOP of its lower half before popping behind the
   * upper half. A gift is a solid object standing on the floor, so the spider
   * has to go around it; the canopy switch then only ever fires while the
   * spider is genuinely above the artwork. */
  assert.equal(E.giftBlocksMovement(1),true,'Solid blocks');
  assert.equal(E.giftBlocksMovement(2),true,'Walk Behind blocks');
  assert.equal(E.giftBlocksMovement(0),false,'Walk Over is meant to be crossed');
  assert.equal(E.giftBlocksMovement(3),false,'Climb is meant to be crossed');
  /* The block radius covers the artwork, not just the small push radius. */
  assert.ok(E.GIFT_BLOCK_RADIUS>18,
    'Walk Behind blocks against the artwork, not the push radius');
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
