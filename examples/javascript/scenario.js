let started=null, hidden=false;
// Map scripts see the entire world. Effects are validated and committed together.
/**
 * @param {import('./glob2').Context} ctx
 * @returns {import('./glob2').Effect[]}
 */
function step(ctx) {
  if (started === null) started = ctx.tick;
  if (ctx.tick === started) {
    return [{type: 'message', text: 'JavaScript scenario running.'}];
  }
  if (!hidden && ctx.tick >= Number(started) + 250) {
    hidden = true;
    return [{type: 'hideMessage'}];
  }
  return [];
}
