const {test, expect} = require('@playwright/test');
const {readFileSync} = require('node:fs');
const {gameURL, waitForMainMenu, clickMainMenu, clickControl, clickByLabel,
  clickSettingsDone} = require('./main-menu');

test.use({locale: 'bg-BG'});
const online = JSON.parse(readFileSync(require('node:path').resolve(__dirname,
  '../../../services/online/packages/i18n/locales/bg.json'), 'utf8'));
const native = readFileSync(require('node:path').resolve(__dirname, '../../../content/data/texts.bg.txt'));

// The HTML launcher must render Bulgarian before WebAssembly or game fonts load.
test('Bulgarian startup uses the packaged Cyrillic font before the game loads', async ({page}, info) => {
  await page.route('**/assets/core*.data', route => route.fulfill({status:503, body:''}));
  await page.goto(gameURL());
  await expect(page.locator('html')).toHaveAttribute('lang', 'bg');
  await expect(page.locator('#loading-status')).toHaveText(online['The game could not be downloaded.']);
  await page.evaluate(() => document.fonts.ready);
  const font = await page.locator('#loading-status').evaluate(element => ({
    family: getComputedStyle(element).fontFamily,
    loaded: [...document.fonts].some(face => face.family.includes('Glob2 Bulgarian') && face.status === 'loaded'),
  }));
  expect(font.family).toContain('Glob2 Bulgarian');
  expect(font.loaded).toBe(true);
  await info.attach('bulgarian-startup', {body:await page.screenshot(), contentType:'image/png'});
});

for (const variant of ['serial', 'threaded']) {
  test(`${variant} game starts in Bulgarian with its complete catalog`, async ({page}, info) => {
    await page.goto(`/?renderer=software&threads=${variant}`);
    await waitForMainMenu(page);
    const state = await page.evaluate(() => ({
      catalog: Array.from(FS.readFile('/data/texts.bg.txt')),
      settingsLabel: (glob2Diagnostics.snapshot().controls['menu/6'] || glob2Diagnostics.snapshot().controls['menu/settings'])?.label,
      executionMode: Module.executionMode,
    }));
    test.skip(variant === 'threaded' && info.project.name === 'webkit' && state.executionMode === 'serial',
      'This WebKit host does not support the threaded runtime capability probe');
    expect(state.executionMode).toBe(variant);
    expect(Buffer.from(state.catalog)).toEqual(native);
    expect(state.settingsLabel).toBe('Настройки');
    await info.attach(`bulgarian-${variant}-menu`, {body:await page.screenshot(), contentType:'image/png'});
    await page.reload();
    await waitForMainMenu(page);
    expect(await page.evaluate(() => Module.executionMode)).toBe(variant);
    expect(await page.evaluate(() => (glob2Diagnostics.snapshot().controls['menu/6'] || glob2Diagnostics.snapshot().controls['menu/settings'])?.label)).toBe('Настройки');
  });
}

test.describe('explicit Bulgarian language selection', () => {
  // An English browser ensures reload cannot pass by detecting Bulgarian again.
  test.use({locale: 'en-US'});
  test('selecting Bulgarian in Settings survives a reload', async ({page}) => {
    test.setTimeout(180000);
    await page.goto('/?renderer=software&threads=serial');
    await waitForMainMenu(page);
    expect(await page.evaluate(() => (glob2Diagnostics.snapshot().controls['menu/6'] || glob2Diagnostics.snapshot().controls['menu/settings'])?.label)).toBe('Settings');
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().assets.translations),
      {timeout:120000}).toBe('ready');
    await clickMainMenu(page, 'settings');
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen)).toContain('SettingsScreen');
    await clickControl(page, 'nav.5'); // Language & player.
    await clickControl(page, 'player.language');
    await clickByLabel(page, /^popup\//, 'Български');
    await clickSettingsDone(page); // Uses the real durable settings save path.
    await waitForMainMenu(page);
    expect(await page.evaluate(() => (glob2Diagnostics.snapshot().controls['menu/6'] || glob2Diagnostics.snapshot().controls['menu/settings'])?.label)).toBe('Настройки');
    await page.reload();
    await waitForMainMenu(page);
    expect(await page.evaluate(() => Module.executionMode)).toBe('serial');
    expect(await page.evaluate(() => (glob2Diagnostics.snapshot().controls['menu/6'] || glob2Diagnostics.snapshot().controls['menu/settings'])?.label)).toBe('Настройки');
    await clickMainMenu(page, 'settings');
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen)).toContain('SettingsScreen');
    await clickControl(page, 'nav.5');
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().controls['player.language']?.label))
      .toBe('Български');
  });
});
