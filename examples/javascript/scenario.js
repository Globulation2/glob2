// Map scripts see the entire world. Effects are validated and committed together.
export function init(ctx, state) {
  state.started = ctx.tick;
  state.hidden = false;
}
export function step(ctx, state) {
  if (ctx.tick === state.started) {
    return [{type: 'message', text: 'JavaScript scenario running.'}];
  }
  if (!state.hidden && ctx.tick >= state.started + 250) {
    state.hidden = true;
    return [{type: 'hideMessage'}];
  }
  return [];
}
