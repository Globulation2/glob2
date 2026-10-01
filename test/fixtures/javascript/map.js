export function init(ctx, state) { state.calls = 0; }
export function step(ctx, state) {
  state.calls++;
  state.random = ctx.random();
  state.math = [Math.sin(1e300), Math.cos(1e300), Math.exp(-745),
    Math.log(Number.MIN_VALUE), Math.sqrt(Number.MIN_VALUE),
    Math.pow(2, state.random), Math.atan2(-0, -0)];
  state.unitCount = ctx.game.units().length;
  state.tiles = ctx.game.map.region(0, 0, 4, 4);
  return [];
}
