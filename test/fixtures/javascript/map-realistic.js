let center=null, density=0, colonies=[], calls=0, random=0;
// An omniscient scenario survey: terrain suitability, a toroidal resource
// centre, and distances to colonies. Keeps results in automatically saved globals.
function step(ctx) {
  const tiles=ctx.game.map.region(0,0,16,16);
  const buildings=ctx.game.buildings({limit:64}).filter(b=>!b.virtual);
  const totals=tiles.reduce((a,t)=>{
    const weight=1+(t.resource.amount??0);
    const angleX=2*Math.PI*t.x/ctx.game.map.width;
    const angleY=2*Math.PI*t.y/ctx.game.map.height;
    a.mass+=weight; a.cx+=weight*Math.cos(angleX); a.sx+=weight*Math.sin(angleX);
    a.cy+=weight*Math.cos(angleY); a.sy+=weight*Math.sin(angleY);
    a.suitability+=Math.log1p(t.fertility??0)*Math.exp(-(t.corners?.[0]??0)/4);
    return a;
  },{mass:0,cx:0,sx:0,cy:0,sy:0,suitability:0});
  const centroid={x:Math.atan2(totals.sx,totals.cx)*ctx.game.map.width/(2*Math.PI),
    y:Math.atan2(totals.sy,totals.cy)*ctx.game.map.height/(2*Math.PI)};
  center=centroid;
  density=totals.suitability/totals.mass;
  colonies=buildings.map(b=>({id:b.id,generation:b.generation,
    distance:Math.hypot(b.x-center.x,b.y-center.y),health:Math.sqrt(b.hp/b.maxHp)}));
  calls=(calls??0)+1;
  random=ctx.random();
  return [];
}
