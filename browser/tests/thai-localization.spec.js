const {test, expect} = require('@playwright/test');
const {waitForMainMenu, gameURL, clickMainMenu, clickControl, clickSettingsDone} = require('./main-menu');
const thai = require('../../platform/packages/i18n/locales/th.json');

test.use({locale: 'th-TH'});

for (const variant of ['serial', 'threaded']) {
  test(`${variant} Thai startup, composed input and refresh use the shipped catalog`, async ({page, context}, info) => {
    const url = new URL(gameURL(), String(info.project.use.baseURL || 'http://127.0.0.1:8770'));
    url.searchParams.set('threads', variant);
    await context.addCookies([{name: 'glob2_locale', value: 'th', url: url.origin}]);
    await page.goto(url.href);
    await waitForMainMenu(page);
    await expect(page.locator('html')).toHaveAttribute('lang', 'th');
    await expect(page.locator('#loading-language')).toHaveValue('th');
    const state = await page.evaluate(() => ({
      labels: Object.values(glob2Diagnostics.snapshot().controls).map(control => control.label),
      catalog: FS.readFile('/data/texts.th.txt', {encoding: 'utf8'}),
      fontSize: FS.stat('/data/fonts/scripts/NotoSansThai-Regular.ttf').size,
      translations: glob2Diagnostics.snapshot().assets.translations,
    }));
    expect(state.labels.some(label => /[\u0e00-\u0e7f]/.test(label))).toBe(true);
    expect(state.catalog).toContain('[language]');
    expect(state.fontSize).toBeGreaterThan(10000);
    expect(state.translations).toBe('ready');
    await page.screenshot({path: info.outputPath(`${variant}-thai-menu.png`)});
    await page.setViewportSize({width: 390, height: 844});
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().width)).toBe(390);
    await page.screenshot({path: info.outputPath(`${variant}-thai-phone-menu.png`)});
    await page.setViewportSize({width: 1200, height: 900});
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().width)).toBe(1200);
    await clickMainMenu(page, 'settings');
    await clickControl(page, 'nav.5');
    await clickControl(page, 'player.name');
    const field = page.locator('input[data-i18n-aria="Game text field"]');
    await expect(field).toBeVisible();
    await expect(field).toHaveAttribute('aria-label', thai['Game text field']);
    await field.evaluate(element => {
      element.dispatchEvent(new CompositionEvent('compositionstart', {bubbles: true}));
      element.value = 'ผู้เล่นไทย 123';
      element.dispatchEvent(new InputEvent('input', {bubbles: true, data: element.value, isComposing: true}));
      element.dispatchEvent(new CompositionEvent('compositionend', {bubbles: true, data: element.value}));
      element.dispatchEvent(new InputEvent('input', {bubbles: true, data: element.value}));
    });
    await expect(field).toHaveValue('ผู้เล่นไทย 123');
    await expect.poll(() => page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'}))).toContain('username=ผู้เล่นไทย 123');
    await page.screenshot({path: info.outputPath(`${variant}-thai-player-input.png`)});
    await clickSettingsDone(page);
    await waitForMainMenu(page);
    await page.evaluate(() => Module.storage.flush());
    await page.reload();
    await waitForMainMenu(page);
    expect(await page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'}))).toContain('username=ผู้เล่นไทย 123');
    await expect(page.locator('html')).toHaveAttribute('lang', 'th');
  });
}
