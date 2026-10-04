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
  const rules = ctx.game.rules();
  // Training-only buildings cannot help when upgrades are disabled. Keep
  // hospitals, swarms and construction staffed: their work is still useful.
  const types = ctx.game.buildingTypes();
  const workersFor = b => {
    const type = types.find(t => t.id === b.type);
    const trainingOnly = type && ['school', 'racetrack', 'swimmingpool'].includes(type.name);
    return rules.noUpgrades && trainingOnly && !type.site ? 0 : 2;
  };
  const building = buildings.find(b => !b.virtual && b.workers !== workersFor(b));
  if (building) {
    return {type: 'workers', building, workers: workersFor(building)};
  }
  return null;
}
