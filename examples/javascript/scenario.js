// Map scripts see the entire world. Effects are validated and committed together.
/** @param {import('./glob2').Context} ctx @param {import('./glob2').State} state */
export function init(ctx, state) {
  state.started = ctx.tick;
  state.hidden = false;
}
/**
 * @param {import('./glob2').Context} ctx
 * @param {import('./glob2').State} state
 * @returns {import('./glob2').Effect[]}
 */
export function step(ctx, state) {
  if (ctx.tick === state.started) {
    return [{type: 'message', text: 'JavaScript scenario running.'}];
  }
  if (!state.hidden && ctx.tick >= Number(state.started) + 250) {
    state.hidden = true;
    return [{type: 'hideMessage'}];
  }
  return [];
}
