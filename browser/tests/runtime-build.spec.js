const {test, expect} = require('@playwright/test');
const {gameURL, clickMainMenu} = require('./main-menu');

test('loading page presents branded progress before the runtime is ready', async ({page}) => {
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  await page.route('**/index.wasm', async route => {
    await gate;
    await route.continue();
  });
  const navigation = page.goto(gameURL());
  await expect(page.locator('#loading')).toBeVisible();
  await expect(page.locator('#loading-status')).not.toBeEmpty();
  expect(await page.locator('body').evaluate(element => getComputedStyle(element).backgroundImage)).not.toBe('none');
  await expect(page.locator('#canvas')).toHaveCSS('opacity', '0');
  release();
  await navigation;
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen)).toContain('MainMenuScreen');
  await expect(page.locator('#loading')).toHaveAttribute('aria-hidden', 'true');
  await expect(page.locator('#canvas')).toHaveCSS('opacity', '1');
});

// The asset-startup host completes before creating Application on the same
// worker. Ending startup must not terminate the replacement host's callbacks.
test('threaded startup transfers its worker lifetime to gameplay and exits cleanly', async ({page}) => {
  test.setTimeout(180000);
  const url = new URL(gameURL(), 'http://localhost');
  url.searchParams.delete('threads');
  await page.goto(url.pathname + url.search);
  await expect.poll(() => page.evaluate(() => globalThis.glob2Diagnostics?.snapshot().screen), {timeout:120000})
    .toContain('MainMenuScreen');
  expect(await page.evaluate(() => Module.executionMode)).toBe('threaded');
  // A replacement host must continue pumping events, not merely publish one
  // successful startup snapshot before its application worker disappears.
  await clickMainMenu(page, 'settings');
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen))
    .toContain('SettingsScreen');
  await page.locator('#canvas').press('Escape');
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen))
    .toContain('MainMenuScreen');
  await clickMainMenu(page, 'quit');
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen))
    .toBe('exited');
});
