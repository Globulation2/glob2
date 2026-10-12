let wheat=[], observedAt=0;
// An AI read-only example. Fog of war is enforced by the host.
// API: docs/scripting/javascript-api.md. Constants are local, not engine globals.
const WHEAT = 1;

/** @param {import('./glob2').Context} ctx */
function step(ctx) {
  const unit = ctx.game.units({team: ctx.myTeam, limit: 1})[0];
  if (!unit) {
    wheat = [];
    return null;
  }

  // Read inputs may be negative: the engine wraps them around the toroidal map.
  const tiles = ctx.game.map.region(unit.x - 4, unit.y - 4, 8, 8);
  wheat = tiles
    .flatMap(t => t.visible && t.resource && t.resource.type === WHEAT
      ? [{x: t.x, y: t.y, amount: t.resource.amount}] : []);
  observedAt = ctx.tick;
  return null;
}
