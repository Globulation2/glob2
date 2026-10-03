const {test, expect} = require('@playwright/test');
const {gameURL} = require('./main-menu');

const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const exists = (page, path) => page.evaluate(path => FS.analyzePath(path).exists, path);

test('the loading page shows download progress and the game starts from the core package', async ({page}) => {
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  await page.route('**/assets/core.*.data', async route => {
    await gate;
    await route.continue();
  });
  const navigation = page.goto(gameURL());
  await expect(page.locator('#loading-progress')).toBeVisible();
  await expect(page.locator('#loading-detail')).toContainText(' MB');
  await expect(page.locator('#loading-bar')).toHaveAttribute('aria-valuemax', '100');
  await expect(page.locator('#loading-status')).toHaveText('Downloading the game…');
  release();
  await navigation;
  await expect.poll(async () => (await snapshot(page)).screen).toContain('MainMenuScreen');
  expect((await snapshot(page)).assets.core).toBe('ready');
  // Simulation data starts with the game; build files never ship.
  for (const path of ['/data/maxima/base.strategy', '/data/nicowar.txt', '/data/usl/Glob2/Runtime/Game.usl',
                      '/data/texts.list.txt', '/data/fonts/sans.ttf', '/maps', '/campaigns/Tutorial_Campaign.txt'])
    expect(await exists(page, path), path).toBe(true);
  for (const path of ['/data/SConscript', '/data/check_translations.py', '/data/fonts/README.md', '/data/screenshots'])
    expect(await exists(page, path), path).toBe(false);
  // In-game music follows in the background.
  await expect.poll(async () => (await snapshot(page)).assets.music, {timeout: 60000}).toBe('ready');
  expect(await exists(page, '/data/zik/original/a1.ogg')).toBe(true);
  const {renderer, assets} = await snapshot(page);
  if (renderer === 'software') expect(assets.hd).toBe('skipped');
  else await expect.poll(async () => (await snapshot(page)).assets.hd, {timeout: 120000}).toBe('ready');
});

test('a failed data download says so and offers to try again', async ({page}) => {
  await page.route('**/assets/core.*.data', route => route.fulfill({status: 503, body: ''}));
  await page.goto(gameURL());
  await expect(page.locator('#loading-status')).toHaveText('The game could not be downloaded.');
  await expect(page.locator('#loading-detail')).toContainText('HTTP 503');
  await expect(page.locator('#loading-retry')).toBeVisible();
  expect((await snapshot(page)).assets.core).toBe('failed');
  await page.unroute('**/assets/core.*.data');
  await page.locator('#loading-retry').click();
  await expect.poll(async () => (await snapshot(page)).screen, {timeout: 60000}).toContain('MainMenuScreen');
});

test('a second visit starts from the packages cached on the device', async ({page, browserName}) => {
  // Playwright's WebKit profile drops Cache Storage across reloads; the loader then
  // falls back to the HTTP cache, which this test server does not make immutable.
  test.skip(browserName === 'webkit', 'automation WebKit does not keep Cache Storage across reloads');
  await page.goto(gameURL());
  await expect.poll(async () => (await snapshot(page)).screen).toContain('MainMenuScreen');
  // Every background package, including the artwork where it is wanted.
  await expect.poll(async () => Object.values((await snapshot(page)).assets).every(state => state === 'ready' || state === 'skipped'),
    {timeout: 180000}).toBe(true);
  const downloads = [];
  page.on('request', request => { if (/\/assets\/.*\.data$/.test(request.url())) downloads.push(request.url()); });
  await page.reload();
  await expect.poll(async () => (await snapshot(page)).screen).toContain('MainMenuScreen');
  await expect.poll(async () => (await snapshot(page)).assets.music, {timeout: 60000}).toBe('ready');
  expect(downloads).toEqual([]);
});
