// A small starting point: keep two workers at each owned building.
export function init(ctx, state) {
  state.decisions = 0;
}
export function step(ctx, state) {
  state.decisions++;
  const buildings = ctx.game.buildings({team: ctx.myTeam});
  const building = buildings.find(b => !b.virtual && b.workers !== 2);
  if (building) return {type: 'workers', building, workers: 2};
  return null;
}
