// A small starting point: keep two requested workers at each owned building.
// API: docs/development/javascript-api.md. Read results are detached copies.
/** @param {import('./glob2').Context} ctx @param {import('./glob2').State} state */
export function init(ctx, state) {
  state.decisions = 0;
}
/**
 * @param {import('./glob2').Context} ctx
 * @param {import('./glob2').State} state
 * @returns {import('./glob2').Order}
 */
export function step(ctx, state) {
  state.decisions = Number(state.decisions) + 1;
  const offset = Number(state.offset ?? 0);
  const buildings = ctx.game.buildings({team: ctx.myTeam, offset, limit: 50});
  state.offset = buildings.length === 50 ? offset + 50 : 0;
  const building = buildings.find(b => !b.virtual && b.workers !== 2);
  if (building) return {type: 'workers', building, workers: 2};
  return null;
}
