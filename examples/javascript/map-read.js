// An AI read-only example. Fog of war is enforced by the host.
// API: docs/development/javascript-api.md. Constants are local, not engine globals.
const WHEAT = 1;

/** @param {import('./glob2').Context} ctx @param {import('./glob2').State} state */
export function step(ctx, state) {
  const unit = ctx.game.units({team: ctx.myTeam, limit: 1})[0];
  if (!unit) {
    state.wheat = [];
    return null;
  }

  // Read inputs may be negative: the engine wraps them around the toroidal map.
  const tiles = ctx.game.map.region(unit.x - 4, unit.y - 4, 8, 8);
  state.wheat = tiles
    .flatMap(t => t.visible && t.resource && t.resource.type === WHEAT
      ? [{x: t.x, y: t.y, amount: t.resource.amount}] : []);
  state.observedAt = ctx.tick;
  return null;
}
