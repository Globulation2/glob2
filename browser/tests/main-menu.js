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

// Mirrors SettingsScreen::layout()'s panel/footer math (src/SettingsScreenLayout.cpp)
// for the footer's two buttons. Assumes the footer status line stays on one
// line, which holds at every viewport this suite resizes to while Settings
// is open; a panel narrower than ~500px could wrap it and shift these.
const settingsFooter = (width, height) => {
  const panelW = Math.min(width - 32, 960);
  const panelH = Math.min(height - 32, 720);
  const panelX = Math.floor((width - panelW) / 2);
  const panelY = Math.floor((height - panelH) / 2);
  const footH = 64;
  const footerX = panelX, footerY = panelY + panelH - footH, footerW = panelW;
  const doneX = footerX + footerW - 112, doneY = footerY + 12;
  return {
    done: {x: doneX + 48, y: doneY + 20},
    // Always visible, and always closes Settings in one click regardless of
    // any save failure — see SettingsScreen::abandon().
    cancel: {x: doneX - 52, y: doneY + 20},
  };
};
exports.settingsFooter = settingsFooter;
exports.clickSettingsDone = (page) => {
  const {width, height} = page.viewportSize();
  return page.locator('#canvas').click({position: settingsFooter(width, height).done, delay: 80});
};
exports.clickSettingsCancel = (page) => {
  const {width, height} = page.viewportSize();
  return page.locator('#canvas').click({position: settingsFooter(width, height).cancel, delay: 80});
};

// Mirrors CustomGameScreen::renderLobby()'s "start" button rect
// (src/CustomGameScreen.cpp). A fresh profile auto-selects a valid premade
// map (FourSquares1), so this alone is enough to launch a match — no map
// or player pick required first.
exports.clickCustomGameStart = (page) => {
  const {width, height} = page.viewportSize();
  const w = Math.min(width - 32, 1120), x = Math.floor((width - w) / 2);
  return page.locator('#canvas').click({position: {x: x + w - 165 + 82, y: height - 52 + 17}, delay: 80});
};
