const point = (width, height, action) => {
  const compact = height < 640;
  const panelX = Math.max(20, Math.min(72, Math.floor(width / 20)));
  const panelH = Math.min(height - 40, 620);
  const panelY = Math.floor((height - panelH) / 2);
  const panelW = compact ? 312 : 368;
  const x = panelX + 24;
  const w = panelW - 48;
  let y = panelY + (compact ? 74 : 110);
  const entries = {};
  const add = (name, h) => {
    entries[name] = {x: x + Math.floor(w / 2), y: y + Math.floor(h / 2)};
    y += h;
  };
  add('custom', compact ? 38 : 46);
  y += 8;
  add('campaign', compact ? 30 : 38);
  y += 6;
  add('load', compact ? 30 : 38);
  y += 6;
  add('tutorial', compact ? 30 : 38);
  y += compact ? 12 : 18;
  add('yog', compact ? 28 : 34);
  y += 4 + (compact ? 12 : 18);
  const utilityH = compact ? 28 : 32;
  const utilityW = Math.floor(w / 2) - 4;
  for (const [index, name] of ['settings', 'editor', 'credits', 'quit'].entries()) {
    entries[name] = {
      x: x + (index % 2) * (Math.floor(w / 2) + 4) + Math.floor(utilityW / 2),
      y: y + Math.floor(index / 2) * (utilityH + 4) + Math.floor(utilityH / 2),
    };
  }
  return entries[action];
};

exports.gameURL = () => {
  const entry = process.env.GLOB2_TEST_ENTRY_PATH || '/';
  const renderer = process.env.GLOB2_TEST_RENDERER;
  if (!renderer) return entry;
  if (!['software', 'webgl2'].includes(renderer)) {
    throw new Error('Unknown test renderer: ' + renderer);
  }
  const url = new URL(entry, 'http://localhost');
  url.searchParams.set('renderer', renderer);
  return url.pathname + url.search;
};

exports.clickMainMenu = async (page, action) => {
  const {width, height} = page.viewportSize();
  return page.locator('#canvas').click({position: point(width, height, action), delay: 80});
};
