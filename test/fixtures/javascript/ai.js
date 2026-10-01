let calls=0, random=0, observed=0;
function step(ctx) {
  calls++;
  random = ctx.random();
  observed = ctx.game.units().length;
  const building = ctx.game.buildings({team: ctx.myTeam}).find(b => !b.virtual);
  const workers = 1 + Math.floor(random * 4);
  return building && building.workers !== workers ? {type: 'workers', building, workers} : null;
}
