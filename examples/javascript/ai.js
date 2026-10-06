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
  const types = ctx.game.buildingTypes();
  const workersFor = b => {
    const type = types.find(t => t.id === b.type);
    if (!type) return 0;
    const hasTraining = type.training.some(service => service.enabled);
    const hasOtherWork = type.site || type.capabilities.some(capability => !capability.startsWith('train'));
    const disabledTrainingOnly = rules.noUpgrades && hasTraining && !hasOtherWork;
    return disabledTrainingOnly ? 0 : Math.min(2, type.maxWorkers);
  };
  const building = buildings.find(b => b.workers !== workersFor(b));
  if (building) {
    return {type: 'workers', building, workers: workersFor(building)};
  }
  return null;
}
