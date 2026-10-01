let calls=0, random=0, math=[], unitCount=0, tiles=[];
function step(ctx) {
  calls++;
  random = ctx.random();
  math = [Math.sin(1e300), Math.cos(1e300), Math.exp(-745),
    Math.log(Number.MIN_VALUE), Math.sqrt(Number.MIN_VALUE),
    Math.pow(2, random), Math.atan2(-0, -0)];
  unitCount = ctx.game.units().length;
  tiles = ctx.game.map.region(0, 0, 4, 4);
  return [];
}
