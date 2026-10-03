let wasHungry = false;
function step(ctx) {
  const workers = ctx.game.units({team:ctx.myTeam,limit:200}).filter(u=>u.type===0);
  const hungry = workers.filter(u=>u.medical===1).length;
  if(hungry>0 && !wasHungry) {
    ctx.wakeAgent({key:'hungry-workers',reason:'Our workers need food.',data:{hungry,tick:ctx.tick}});
  }
  wasHungry = hungry>0;
  return {output:{hungry,observedWorkers:workers.length}};
}
