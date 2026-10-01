// An omniscient scenario survey: terrain suitability, a toroidal resource
// centre, and distances to colonies. Returns data through explicit saved state.
export function step(ctx,state) {
  const tiles=ctx.game.map.region(0,0,16,16);
  const colonies=ctx.game.buildings({limit:64}).filter(b=>!b.virtual);
  const totals=tiles.reduce((a,t)=>{
    const weight=1+(t.resource.amount??0);
    const angleX=2*Math.PI*t.x/ctx.game.map.width;
    const angleY=2*Math.PI*t.y/ctx.game.map.height;
    a.mass+=weight; a.cx+=weight*Math.cos(angleX); a.sx+=weight*Math.sin(angleX);
    a.cy+=weight*Math.cos(angleY); a.sy+=weight*Math.sin(angleY);
    a.suitability+=Math.log1p(t.fertility??0)*Math.exp(-(t.terrain??0)/4);
    return a;
  },{mass:0,cx:0,sx:0,cy:0,sy:0,suitability:0});
  const center={x:Math.atan2(totals.sx,totals.cx)*ctx.game.map.width/(2*Math.PI),
    y:Math.atan2(totals.sy,totals.cy)*ctx.game.map.height/(2*Math.PI)};
  state.center=center;
  state.density=totals.suitability/totals.mass;
  state.colonies=colonies.map(b=>({id:b.id,generation:b.generation,
    distance:Math.hypot(b.x-center.x,b.y-center.y),health:Math.sqrt(b.hp/b.maxHp)}));
  state.calls=(state.calls??0)+1;
  state.random=ctx.random();
  return [];
}
