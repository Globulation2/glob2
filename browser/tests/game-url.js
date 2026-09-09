// Apply the same renderer selection a player can request in the address bar.
// This lets the complete existing suite qualify either backend.
function gameURL() {
  const entry = process.env.GLOB2_TEST_ENTRY_PATH || '/';
  const renderer = process.env.GLOB2_TEST_RENDERER;
  if (!renderer) return entry;
  if (!['software', 'webgl2'].includes(renderer)) throw new Error('Unknown test renderer: ' + renderer);
  const url = new URL(entry, 'http://localhost');
  url.searchParams.set('renderer', renderer);
  return url.pathname + url.search;
}
module.exports = {gameURL};
