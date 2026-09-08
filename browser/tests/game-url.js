// Apply the same renderer selection a player can request in the address bar.
// This lets the complete existing suite qualify either backend.
function gameURL() {
  const renderer = process.env.GLOB2_TEST_RENDERER;
  if (!renderer) return '/';
  if (!['software', 'webgl2'].includes(renderer)) throw new Error('Unknown test renderer: ' + renderer);
  return '/?renderer=' + renderer;
}
module.exports = {gameURL};
