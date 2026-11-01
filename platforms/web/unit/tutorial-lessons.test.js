const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const root=path.resolve(__dirname,'../..');
const read=name=>fs.readFileSync(path.join(root,name),'utf8');
function runner(definition,hook='function tutorialCondition(){return false;}') {
  const context=vm.createContext({});
  vm.runInContext('const lesson='+JSON.stringify(definition)+';'+hook+read('scripts/tutorials/runtime.js'),context);
  return {step:ctx=>context.step(ctx),state:()=>JSON.parse(vm.runInContext('JSON.stringify(tutorialState)',context)),context};
}
const definition=()=>JSON.parse(read('scripts/tutorials/introduction.json'));
const areas={home:{x:86,y:91,width:4,height:4},lakeShore:{x:83,y:84,width:4,height:4},schoolSite:{x:89,y:97,width:2,height:2},landmark:{x:31,y:28,width:7,height:9},enemyOutpost:{x:43,y:27,width:10,height:12},enemyApproach:{x:39,y:0,width:16,height:64}};
const game=(buildings=[],units=[])=>({map:{area:name=>areas[name]},buildings:({team})=>buildings.filter(b=>b.team===team),units:({team})=>units.filter(u=>u.team===team)});
const view=effects=>effects.find(e=>e.type==='tutorial')?.view;
test('moving a named landmark updates both completion and its screen destination',()=>{
  const d=definition();d.confirmTicks=0;d.targets.landmark={kind:'area',x:8,y:20,width:7,height:9};
  const r=runner(d),flag={id:4,generation:1,team:0,shortType:8,workers:1,x:9,y:22};
  const ctx={tick:0,game:game([flag],[])};
  const first=view(r.step(ctx));assert.equal(first.destination.x,11.5);assert.equal(first.destination.y,24.5);
  ctx.tick=1;ctx.game=game([flag],[{id:1,generation:1,team:0,type:1,x:9,y:22,attachedBuilding:flag}]);
  const second=view(r.step(ctx));assert.equal(second.stepId,'build');
});
test('reused entity IDs with a new generation count as new buildings',()=>{
  const d=JSON.parse(read('scripts/tutorials/food-supply.json'));d.confirmTicks=0;
  const r=runner(d),inn={id:1,generation:1,team:0,shortType:1,construction:0,workers:1,resources:[0,0]};
  assert.equal(view(r.step({tick:0,game:game([inn])})).stepId,'build');
  const rebuilt={...inn,generation:2};
  assert.equal(view(r.step({tick:1,game:game([rebuilt])})).stepId,'supply');
  rebuilt.resources[1]=1;
  assert.equal(view(r.step({tick:2,game:game([rebuilt])})).stepId,'complete');
});
test('stable step IDs survive saved runner state without positional UI assumptions',()=>{
  const d=definition();d.confirmTicks=0;
  const r=runner(d);r.step({tick:0,game:game()});
  const state=r.state();assert.equal(state.stepId,'scout');
  const resumed=runner(d);resumed.context.saved=JSON.stringify(state);
  vm.runInContext('tutorialState=JSON.parse(saved)',resumed.context);
  assert.deepEqual(JSON.parse(JSON.stringify(resumed.step({tick:1,game:game()}))),JSON.parse(JSON.stringify(r.step({tick:1,game:game()}))));
});
test('custom conditions extend the runner without a native callback',()=>{
  const d=definition();d.confirmTicks=0;d.steps[0].complete={type:'custom',name:'ready'};
  const r=runner(d,'function tutorialCondition(name,ctx){return name==="ready" && ctx.tick===7;}');
  r.step({tick:0,game:game()});assert.equal(view(r.step({tick:7,game:game()})).stepId,'build');
});
test('bound patient identity persists as health changes and rejects ID reuse',()=>{
  const d=definition();d.confirmTicks=0;
  d.targets.patient={kind:'unit',team:0,type:0,bind:'lesson',filters:[{field:'hp',op:'lte',value:50}]};
  d.steps[0].complete={type:'increased',target:'patient',field:'hp',baseline:'lesson'};
  const r=runner(d),patient={id:7,generation:1,type:0,team:0,hp:40};
  r.step({tick:0,game:game([], [patient])});
  assert.equal(view(r.step({tick:1,game:game([], [{...patient,generation:2,hp:100}])}))?.stepId,undefined);
  assert.equal(view(r.step({tick:2,game:game([], [{...patient,hp:100}])})).stepId,'build');
});
test('already trained units and idle ticks cannot satisfy improvement goals',()=>{
  const d=definition();d.confirmTicks=0;
  d.steps[0].complete={type:'increased',target:'workers',field:'levels.3',baseline:'lesson'};
  const r=runner(d),worker={id:2,generation:1,type:0,team:0,levels:[0,0,0,1]};
  r.step({tick:0,game:game([], [worker])});
  r.step({tick:2000,game:game([], [worker])});assert.equal(r.state().stepId,'scout');
  assert.equal(view(r.step({tick:2001,game:game([], [{...worker,levels:[0,0,0,2]}])})).stepId,'build');
});
test('guard encounters issue one ordinary enemy order and stay issued after resume',()=>{
  const d=definition();d.confirmTicks=0;
  d.steps[0].complete={type:'zoneCoverage',area:'landmark',zone:'guard',team:0,minimum:4};
  d.steps[1].onEnter=[{type:'scenarioOrder',team:1,order:{type:'create',buildingName:'warflag',destination:'landmark',workers:2,futureWorkers:2,range:3}}];
  const r=runner(d),ctx={tick:0,game:{...game(),map:{area:name=>areas[name],zones:()=>[]},buildingTypes:()=>[{id:40,name:'warflag',site:false,level:0}]}};
  r.step(ctx);ctx.tick=1;ctx.game.map.zones=()=>Array(4).fill({guard:true});
  const effects=r.step(ctx);assert.equal(effects.filter(e=>e.type==='scenarioOrder').length,1);
  const order=effects.find(e=>e.type==='scenarioOrder').order;assert.equal(order.buildingType,40);assert.equal(order.x,34);assert.equal(order.y,32);
  const resumed=runner(d);resumed.context.saved=JSON.stringify(r.state());vm.runInContext('tutorialState=JSON.parse(saved)',resumed.context);
  ctx.tick=2;assert.equal(resumed.step(ctx).filter(e=>e.type==='scenarioOrder').length,0);
});

test('warrior training accepts learning ahead and replacement warriors',()=>{
  for(const trainedAhead of [false,true]) {
    const d=JSON.parse(read('scripts/tutorials/warrior-training.json'));d.confirmTicks=0;
    const r=runner(d),swarm={id:0,generation:1,team:0,shortType:0};
    r.step({tick:0,game:game([swarm])});
    const warrior=id=>({id,generation:1,team:0,type:2,levels:Array.from({length:17},(_,i)=>i===9 && trainedAhead ? 1 : 0)});
    const units=[warrior(1),warrior(2)],camp={id:3,generation:1,team:0,shortType:5,construction:0};
    assert.equal(view(r.step({tick:1,game:game([swarm,camp],units)})).stepId,'build');
    assert.equal(view(r.step({tick:2,game:game([swarm,camp],units)})).stepId,'train');
    if(!trainedAhead) {
      r.step({tick:100,game:game([swarm,camp],units)});assert.equal(r.state().stepId,'train');
      // Both initial warriors may be lost; a newly produced trained warrior counts.
      units.splice(0,units.length,{...warrior(4),levels:Array.from({length:17},(_,i)=>i===9 ? 1 : 0)});
    }
    assert.equal(view(r.step({tick:101,game:game([swarm,camp],units)})).stepId,'complete');
  }
});

test('lost bound patient gives restart guidance after save/load despite ID reuse',()=>{
  const d=JSON.parse(read('scripts/tutorials/healing.json'));d.confirmTicks=0;
  const r=runner(d),swarm={id:0,generation:1,team:0,shortType:0};
  const patient={id:1,generation:1,team:0,type:0,hp:25,maxHp:100};
  r.step({tick:0,game:game([swarm],[patient])});
  const hospital={id:2,generation:1,team:0,shortType:2,construction:0};
  assert.equal(view(r.step({tick:1,game:game([swarm,hospital],[patient])})).stepId,'heal');
  const resumed=runner(d);resumed.context.saved=JSON.stringify(r.state());vm.runInContext('tutorialState=JSON.parse(saved)',resumed.context);
  const result=view(resumed.step({tick:10000,game:game([swarm,hospital],[{...patient,generation:2,hp:100}])}));
  assert.equal(result.stepId,'heal');assert.equal(result.hint,'[intro hint retry]');
});

test('losing bound trainees while replacement workers survive offers a restart',()=>{
  for(const [name,buildingType] of [['walking-training',3],['swimming',4],['schools',6],['upgrades',3]]) {
    const d=JSON.parse(read('scripts/tutorials/'+name+'.json'));d.confirmTicks=0;
    const r=runner(d),swarm={id:0,generation:1,team:0,shortType:0};
    const worker={id:1,generation:1,team:0,type:0,levels:Array(17).fill(0)};
    const initial=name==='upgrades' ? [{id:2,generation:1,team:0,shortType:3,construction:0,level:0}] : [];
    r.step({tick:0,game:game([swarm,...initial],[worker])});
    const building={id:2,generation:1,team:0,shortType:buildingType,construction:0,level:1};
    r.step({tick:1,game:game([swarm,building],[worker])});
    const result=view(r.step({tick:10000,game:game([swarm,building],[{...worker,generation:2}])}));
    assert.equal(result.hint,'[intro hint retry]',name);
    assert.notEqual(result.stepId,'complete',name);
  }
});
