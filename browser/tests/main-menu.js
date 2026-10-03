// Screens and dialogs publish their interactive controls, keyed by the same
// stable keys the native harnesses use, with bounds in logical pixels (see
// ApplicationHost::controlsChanged and glob2Diagnostics.snapshot().controls).
// Tests drive those real controls rather than mirroring layout arithmetic.
const {expect} = require('@playwright/test');
// Layout runs inside the game's animation frame; two frames after a resize or
// rebuild the published bounds are current.
const settled = page => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
// The logical surface always fills the window at one uniform scale, so bounds
// published for a previous window size (their surface has another aspect
// ratio) are stale after a resize and must not be clicked.
function current(page, bounds) {
  const {width, height} = page.viewportSize();
  return Math.abs(width / bounds.surface.w - height / bounds.surface.h) < 0.02;
}
async function control(page, key, {timeout = 30000, enabled = true} = {}) {
  let found;
  await expect.poll(async () => {
    found = await page.evaluate(key => glob2Diagnostics.snapshot().controls[key] || null, key);
    return Boolean(found && (!enabled || found.enabled) && current(page, found));
  }, {timeout, message: `control "${key}" is not available`}).toBe(true);
  await settled(page);
  return (await page.evaluate(key => glob2Diagnostics.snapshot().controls[key] || null, key)) || found;
}
// The first control whose key matches and whose text is the label shown.
exports.clickByLabel = async (page, keyPattern, label, options = {}) => {
  const source = keyPattern.source;
  let key;
  await expect.poll(async () => {
    key = await page.evaluate(({source, label}) => {
      const pattern = new RegExp(source);
      for (const [key, value] of Object.entries(glob2Diagnostics.snapshot().controls))
        if (pattern.test(key) && value.label.toLowerCase() === label.toLowerCase()) return key;
      return null;
    }, {source, label});
    return Boolean(key);
  }, {timeout: options.timeout || 30000, message: `no control matching ${source} labelled "${label}"`}).toBe(true);
  return exports.clickControl(page, key, options);
};
// Logical → CSS pixels: the canvas fills the window at the logical surface size,
// scaled by any interface scale setting.
function css(page, bounds, point) {
  const {width, height} = page.viewportSize();
  const sx = width / bounds.surface.w, sy = height / bounds.surface.h;
  return {x: point.x * sx, y: point.y * sy};
}
function center(page, bounds) {
  return css(page, bounds, {x: bounds.x + bounds.w / 2, y: bounds.y + bounds.h / 2});
}
exports.control = control;
// The control's bounds in CSS pixels, for pixel checks near a known control.
exports.controlBox = async (page, key, options) => {
  const bounds = await control(page, key, {enabled: false, ...options});
  const origin = css(page, bounds, {x: bounds.x, y: bounds.y});
  const corner = css(page, bounds, {x: bounds.x + bounds.w, y: bounds.y + bounds.h});
  return {x: origin.x, y: origin.y, width: corner.x - origin.x, height: corner.y - origin.y};
};
// The panel of the host owning a control, in CSS pixels.
exports.rootBox = async (page, key) => {
  const bounds = await control(page, key, {enabled: false});
  const origin = css(page, bounds, {x: bounds.root.x, y: bounds.root.y});
  const corner = css(page, bounds, {x: bounds.root.x + bounds.root.w, y: bounds.root.y + bounds.root.h});
  return {x: origin.x, y: origin.y, width: corner.x - origin.x, height: corner.y - origin.y};
};
// Scroll regions publish every row and card, including ones scrolled out of
// the window; wheel over the owning panel until the control is on screen.
// A control's "visible" rect is what its scroll regions leave on screen.
// Game frames are scheduled by the host, not by requestAnimationFrame, so a
// scroll shows up in the published bounds only after the next game frame.
const same = (a, b) => a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h;
const shown = bounds => bounds.visible && bounds.visible.w >= Math.min(bounds.w, 8) && bounds.visible.h >= Math.min(bounds.h, 8);
async function controlOnScreen(page, key, options) {
  let bounds = await control(page, key, options);
  for (let attempt = 0; attempt < 60; ++attempt) {
    if (shown(bounds)) {
      // Wait until the bounds hold still before clicking.
      await page.waitForTimeout(150);
      const again = await control(page, key, options);
      if (same(again, bounds) && same(again.visible, bounds.visible)) return bounds;
      bounds = again;
      continue;
    }
    const root = css(page, bounds, {x: bounds.root.x + bounds.root.w / 2, y: bounds.root.y + bounds.root.h / 2});
    await page.mouse.move(root.x, root.y);
    await page.mouse.wheel(0, bounds.y + bounds.h / 2 < bounds.root.y + bounds.root.h / 2 ? -120 : 120);
    await page.waitForTimeout(150);
    bounds = await control(page, key, options);
  }
  throw new Error(`control "${key}" cannot be scrolled into view`);
}
// Click the on-screen part of the control.
function visibleCenter(page, bounds) {
  const v = shown(bounds) ? bounds.visible : bounds;
  return css(page, bounds, {x: v.x + v.w / 2, y: v.y + v.h / 2});
}
// Input is consumed at the game's next host frame; give that frame a moment
// to run so a test's next step (a resize, say) cannot overtake the click. A
// test that holds the loader pauses frames, so this never blocks for long.
async function consumed(page) {
  const loop = () => page.evaluate(() => glob2Diagnostics.snapshot().loop);
  const before = await loop();
  const deadline = Date.now() + 500;
  while (Date.now() < deadline) {
    if ((await loop()) > before) return;
    await page.waitForTimeout(20);
  }
}
exports.consumed = consumed;
exports.clickControl = async (page, key, options = {}) => {
  const bounds = await controlOnScreen(page, key, options);
  await page.locator('#canvas').click({position: visibleCenter(page, bounds), delay: 80, ...(options.click || {})});
  await consumed(page);
};
exports.tapControl = async (page, key, options = {}) => {
  const bounds = await control(page, key, options);
  const at = center(page, bounds);
  await page.touchscreen.tap(at.x, at.y);
  await consumed(page);
};
// List rows are published as "<list>/<index>" with the text they show.
exports.clickListRow = async (page, list, match, options = {}) => {
  let key;
  await expect.poll(async () => {
    const rows = await page.evaluate(list => {
      const controls = glob2Diagnostics.snapshot().controls;
      return Object.entries(controls).filter(([key]) => key.startsWith(list + '/')).map(([key, value]) => ({key, label: value.label}));
    }, list);
    const row = typeof match === 'number' ? rows.find(row => row.key === list + '/' + match)
      : rows.find(row => match instanceof RegExp ? match.test(row.label) : row.label === match);
    key = row?.key;
    return Boolean(key);
  }, {timeout: options.timeout || 30000, message: `row ${match} of list "${list}" is not available`}).toBe(true);
  return exports.clickControl(page, key, options);
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

const mainMenuCodes = {campaign:0, tutorial:1, load:2, custom:3, yog:4, lan:5, settings:6, editor:7, credits:8, quit:9};
exports.clickMainMenu = async (page, action) => {
  const code = mainMenuCodes[action];
  if (code === undefined) throw new Error('Unknown main menu action: ' + action);
  const keys = action === 'settings' ? ['menu/6', 'menu/settings'] : ['menu/' + code];
  const present = () => page.evaluate(keys => {
    const controls = glob2Diagnostics.snapshot().controls;
    return keys.find(key => controls[key]) || (controls['menu/more'] ? 'menu/more' : null);
  }, keys);
  await expect.poll(present, {timeout: 30000, message: 'main menu is not showing'}).not.toBeNull();
  let key = await present();
  // Phones keep the utilities one level away behind More.
  if (key === 'menu/more' && !keys.includes('menu/more')) {
    await exports.clickControl(page, 'menu/more');
    key = keys[0];
  }
  return exports.clickControl(page, key);
};

exports.clickSettingsDone = page => exports.clickControl(page, 'done');
// Leaves Settings without claiming a durable save. Changes save as they are made,
// so the footer has only Done; a failed save adds "continue" (SettingsScreen::
// abandon()), which closes at once. Done after a failed restore fails the same
// way and offers it.
exports.clickSettingsCancel = async page => {
  await control(page, 'done', {enabled: false});
  // A save that has just failed shows "continue" at the next layout.
  let offered = false;
  for (const end = Date.now() + 2000; !offered && Date.now() < end; await page.waitForTimeout(100))
    offered = await page.evaluate(() => Boolean(glob2Diagnostics.snapshot().controls.cancel));
  if (!offered) await exports.clickControl(page, 'done');
  return exports.clickControl(page, 'cancel');
};

// The lobby's Start ignores input while its preview is pending; wait for the
// same ready state the lobby publishes.
exports.clickCustomGameStart = async page => {
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().customGameReady),
    {timeout: 60000}).toBe(true);
  return exports.clickControl(page, 'start');
};
// The AI profile picker behind a colony's info button on the Players & Teams tab.
exports.clickCustomAIProfile = (page, colony) => exports.clickControl(page, `colony/${colony}/info`);

// Exercise the visible editing surface with actual mouse and keyboard events.
// Filling the DOM value directly would miss focus, deletion and key routing bugs.
// The browser input exists while a field is being edited, so start with the
// field's own control (by key) as a player would.
exports.editTextField = async (page, value, password = false, key = password ? 'password' : 'name') => {
  await exports.clickControl(page, key);
  const field = page.locator(password ? 'input[aria-label="Password"]' : 'input[aria-label="Game text field"]');
  await field.click();
  await expect(field).toBeFocused();
  // Use the platform's native Select All shortcut; Home/End caret behavior
  // differs between macOS Firefox and the other browser/platform pairs.
  await field.press('ControlOrMeta+A');
  await field.press('Delete');
  await expect(field).toHaveValue('');
  await field.pressSequentially(value);
  await expect(field).toHaveValue(value);
  if (password) await expect(field).toHaveAttribute('type', 'password');
};

exports.clickResultsSave = page => exports.clickControl(page, 'save-replay');

// Export binary evidence compactly: tracing a JS number per byte can consume
// minutes and hundreds of MB for a replay. This leaves the exact bytes intact.
exports.readBrowserFile = async (page,path) => Buffer.from(await page.evaluate(path=>{
  const data=FS.readFile(path),parts=[];
  for(let at=0;at<data.length;at+=32768)
    parts.push(String.fromCharCode(...data.subarray(at,at+32768)));
  return btoa(parts.join(''));
},path),'base64');
