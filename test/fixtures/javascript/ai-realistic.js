let calls=0, scores=[], metrics={};
// A small economic planner: score visible resources by distance and demand,
// estimate local pressure, and return a legal worker assignment.
function step(ctx) {
  calls++;
  const units=ctx.game.units({team:ctx.myTeam,limit:64});
  const buildings=ctx.game.buildings({team:ctx.myTeam,limit:32});
  const types=ctx.game.buildingTypes();
  const home=buildings.find(b=>!b.virtual);
  if (!home) { scores=[]; return null; }
  const tiles=ctx.game.map.region(home.x-4,home.y-4,8,8);
  const distance=(x,y)=>{
    const dx=Math.min(Math.abs(x-home.x),ctx.game.map.width-Math.abs(x-home.x));
    const dy=Math.min(Math.abs(y-home.y),ctx.game.map.height-Math.abs(y-home.y));
    return Math.hypot(dx,dy);
  };
  const ranked=tiles.filter(t=>t.explored && t.resource.amount>0).map(tile=>{
    const travel=distance(tile.x,tile.y);
    const value=Math.log1p(tile.resource.amount)/(1+travel)**1.5;
    return {x:tile.x,y:tile.y,type:tile.resource.type,distance:travel,value};
  });
  ranked.sort((a,b)=>b.value-a.value || a.type-b.type || a.y-b.y || a.x-b.x);
  const pressure=units.reduce((sum,u)=>sum+Math.sqrt(Math.max(0,u.hp))/Math.max(1,u.maxHp),0);
  const potential=ranked.reduce((sum,s)=>sum+s.value,0);
  const demand=types.filter(t=>!t.virtual && t.level===0).length;
  const jitter=ctx.random()/1024;
  const workers=Math.max(1,Math.min(20,Math.round(Math.log1p(pressure+potential+demand)+jitter)));
  scores=ranked.slice(0,8);
  metrics={pressure,potential,demand,jitter,workers};
  return {type:'workers',building:home,workers};
}
