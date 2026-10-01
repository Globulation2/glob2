export function init(ctx, state) { state.calls = 0; }
export function step(ctx, state) {
  state.calls++;
  state.random = ctx.random();
  state.observed = ctx.game.units().length;
  const building = ctx.game.buildings({team: ctx.myTeam}).find(b => !b.virtual);
  const workers = 1 + Math.floor(state.random * 4);
  return building && building.workers !== workers ? {type: 'workers', building, workers} : null;
}
