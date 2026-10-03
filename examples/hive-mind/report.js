function step(ctx) {
  const buildings = ctx.game.buildings({team:ctx.myTeam,limit:200});
  return {output:{tick:ctx.tick,buildings:buildings.map(b=>({id:b.id,generation:b.generation,type:b.type,workers:b.workers}))}};
}
