function step(ctx) {
  const swarms = ctx.game.buildings({team:ctx.myTeam,limit:200}).filter(b=>b.shortType===0 && b.construction===0);
  return {output:{swarms:swarms.length},orders:swarms.map(b=>({type:'production',building:{id:b.id,generation:b.generation},ratios:[4,1,1]})).slice(0,32)};
}
