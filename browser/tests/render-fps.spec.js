// SPDX-License-Identifier: GPL-3.0-or-later
const {test, expect} = require('@playwright/test');
const {gameURL, clickMainMenu, clickControl, clickSettingsDone} = require('./main-menu');
const screen = (page, name) => expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen)).toContain(name);
const savedFps = page => page.evaluate(() => {
  const text = FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding:'utf8'});
  return Number(text.match(/^targetRenderFps=(\d+)$/m)?.[1]);
});

for (const compact of [false, true]) {
  test(`render FPS selection persists (${compact ? 'compact' : 'desktop'})`, async ({page}, info) => {
    if (compact) await page.setViewportSize({width:460, height:820});
    await page.goto(gameURL());
    await screen(page, 'MainMenuScreen');
    await clickMainMenu(page, 'settings');
    await screen(page, 'SettingsScreen');
    await clickControl(page, 'graphics.fps');
    await page.screenshot({path:info.outputPath('target-render-fps.png')});
    await clickControl(page, 'popup/8');
    await clickSettingsDone(page);
    await screen(page, 'MainMenuScreen');
    expect(await savedFps(page)).toBe(0);
    await page.setViewportSize(compact ? {width:820, height:460} : {width:1000, height:700});
    await clickMainMenu(page, 'settings');
    await screen(page, 'SettingsScreen');
    await clickSettingsDone(page);
    await screen(page, 'MainMenuScreen');
    await page.reload();
    await screen(page, 'MainMenuScreen');
    expect(await savedFps(page)).toBe(0);
    await clickMainMenu(page, 'settings');
    await screen(page, 'SettingsScreen');
    await clickControl(page, 'graphics.fps');
    await clickControl(page, 'popup/2');
    await clickSettingsDone(page);
    await screen(page, 'MainMenuScreen');
    expect(await savedFps(page)).toBe(60);
  });
}

// Dialog updates use timers while painting uses animation-frame callbacks.
// DOM changes/actions must never be delivered from the paint callback.
test('painting does not dispatch browser text actions', async ({page}) => {
  const url = new URL(gameURL(), 'http://localhost');
  url.searchParams.set('threads', 'serial');
  await page.goto(url.pathname + url.search);
  await screen(page, 'MainMenuScreen');
  await clickMainMenu(page, 'settings');
  await screen(page, 'SettingsScreen');
  await clickControl(page, 'nav.5'); // Player category.
  await clickControl(page, 'player.name');
  const field = page.locator('input[aria-label="Game text field"]');
  await expect(field).toBeVisible();
  await page.evaluate(() => {
    window.textDispatch = {updates:0, paints:0, actions:0};
    const request = window.requestAnimationFrame;
    window.requestAnimationFrame = callback => request.call(window, time => {
      window.insideAnimationFrame = true;
      try { callback(time); } finally { window.insideAnimationFrame = false; }
    });
    const take = Module.textBridge.take;
    Module.textBridge.take = function(id) {
      const change = take.call(this, id);
      window.textDispatch[window.insideAnimationFrame ? 'paints' : 'updates']++;
      if (change?.action) window.textDispatch.actions++;
      return change;
    };
  });
  await field.fill('Render pacing review');
  await field.press('Enter');
  await expect.poll(() => page.evaluate(() => window.textDispatch.actions)).toBeGreaterThan(0);
  const delivered = await page.evaluate(() => window.textDispatch);
  expect(delivered.updates).toBeGreaterThan(0);
  expect(delivered.paints).toBe(0);
  await clickSettingsDone(page);
  await screen(page, 'MainMenuScreen');
});
