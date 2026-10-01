let decisions=0, offset=0;
// A small starting point: keep two requested workers at each owned building.
// API: docs/development/javascript-api.md. Read results are detached copies.
/**
 * @param {import('./glob2').Context} ctx
 * @returns {import('./glob2').Order}
 */
function step(ctx) {
  decisions++;
  const buildings = ctx.game.buildings({team: ctx.myTeam, offset, limit: 50});
  offset = buildings.length === 50 ? offset + 50 : 0;
  const building = buildings.find(b => !b.virtual && b.workers !== 2);
  if (building) return {type: 'workers', building, workers: 2};
  return null;
}
