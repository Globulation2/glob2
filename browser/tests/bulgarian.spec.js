const {test, expect} = require('@playwright/test');
const {readFileSync} = require('node:fs');
const {gameURL, waitForMainMenu} = require('./main-menu');

test.use({locale: 'bg-BG'});
const online = JSON.parse(readFileSync(require('node:path').resolve(__dirname,
  '../../platform/packages/i18n/locales/bg.json'), 'utf8'));

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
      catalog: FS.readFile('/data/texts.bg.txt', {encoding:'utf8'}),
      labels: Object.values(glob2Diagnostics.snapshot().controls).map(control => control.label),
    }));
    expect(state.catalog).toContain('[language-code]\nbg\n');
    expect(state.labels.some(label => /[А-Яа-я]/.test(label))).toBe(true);
    await info.attach(`bulgarian-${variant}-menu`, {body:await page.screenshot(), contentType:'image/png'});
    await page.reload();
    await waitForMainMenu(page);
    const labels = await page.evaluate(() => Object.values(glob2Diagnostics.snapshot().controls).map(control => control.label));
    expect(labels.some(label => /[А-Яа-я]/.test(label))).toBe(true);
  });
}
