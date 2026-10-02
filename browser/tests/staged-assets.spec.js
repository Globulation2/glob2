const {test, expect} = require('@playwright/test');
const {gameURL, clickMainMenu, clickCustomGameStart} = require('./main-menu');

// Data that follows the main menu (scons/web_assets.py): the in-game sprites, the
// full font and the menu music. Native builds load all of it at startup.
const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name, timeout = 60000) =>
  expect.poll(async () => (await snapshot(page)).screen, {timeout}).toContain(name);
// Hold a package's download until the returned function is called.
async function hold(page, name) {
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  await page.route(`**/assets/${name}*.data`, async route => { await gate; await route.continue(); });
  return release;
}

// Choose the interface language for the next visit, as the settings screen would.
// The game may write its preferences just after the menu appears, so check that
// the choice is what reached storage.
async function chooseLanguage(page, code) {
  await expect.poll(async () => page.evaluate(async code => {
    const path = '/home/web_user/.glob2/preferences.txt';
    const read = () => FS.analyzePath(path).exists ? FS.readFile(path, {encoding: 'utf8'}) : '';
    if (!read().includes('language=' + code + '\n')) {
      FS.writeFile(path, read().replace(/^language=.*\n?/m, '') + 'language=' + code + '\n');
      await Module.storage.flush();
    }
    await new Promise(resolve => setTimeout(resolve, 500));
    return read().includes('language=' + code + '\n');
  }, code)).toBe(true);
  // Let the browser finish committing IndexedDB before the page goes away.
  await page.waitForTimeout(1500);
}

test('the menus work before the game sprites arrive, and a match waits for them', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  page.on('console', message => { if (/Can't load sprite|abort/i.test(message.text())) errors.push(message.text()); });
  const release = await hold(page, 'game');
  await page.goto(gameURL());
  await screen(page, 'MainMenuScreen');
  expect((await snapshot(page)).assets.game).toBe('downloading');
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
  await clickCustomGameStart(page);
  await screen(page, 'GameLoadScreen');
  // The load screen waits ("Loading game graphics") without starting the match.
  await page.waitForTimeout(3000);
  expect((await snapshot(page)).screen).toContain('GameLoadScreen');
  expect((await snapshot(page)).tick).toBe(0);
  release();
  await expect.poll(async () => (await snapshot(page)).assets.game, {timeout: 60000}).toBe('ready');
  await expect.poll(async () => (await snapshot(page)).tick, {timeout: 60000}).toBeGreaterThan(25);
  expect(errors).toEqual([]);
});

test('later packages arrive in the background and replace the core font', async ({page}) => {
  await page.goto(gameURL());
  await screen(page, 'MainMenuScreen');
  for (const name of ['game', 'menu-music', 'font-cjk', 'translations'])
    await expect.poll(async () => (await snapshot(page)).assets[name], {timeout: 120000}).toBe('ready');
  // The full font now has the CJK outlines the core copy leaves out.
  const size = await page.evaluate(() => FS.stat('/data/fonts/sans.ttf').size);
  expect(size).toBeGreaterThan(4e6);
  expect(await page.evaluate(() => FS.analyzePath('/data/zik/menu.ogg').exists)).toBe(true);
  // The reloaded string table still drives the menu.
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
});

test('a Chinese interface downloads the full font before the game starts', async ({page, browserName}) => {
  // Firefox under automation occasionally restores the profile from before the
  // test's direct preferences write (about 1 run in 10); the setting screen's own
  // save path is not involved here.
  test.skip(browserName === 'firefox', 'the direct preferences write does not always survive a Firefox reload');
  // Keep the font off this device so the second visit has to download it.
  const release = await hold(page, 'font-cjk');
  await page.goto(gameURL());
  await screen(page, 'MainMenuScreen');
  await chooseLanguage(page, 'zh-cn');
  await page.reload();
  await expect(page.locator('#loading-status')).toHaveText('Downloading the game…');
  await page.waitForTimeout(3000);
  expect((await snapshot(page)).screen).toBe('loading');
  release();
  await screen(page, 'MainMenuScreen');
  expect((await snapshot(page)).assets['font-cjk']).toBe('ready');
  expect(await page.evaluate(() => FS.stat('/data/fonts/sans.ttf').size)).toBeGreaterThan(4e6);
});

test('an interface in another language downloads its translations before the game starts', async ({page, browserName}) => {
  // Firefox under automation occasionally restores the profile from before the
  // test's direct preferences write (about 1 run in 10); the setting screen's own
  // save path is not involved here.
  test.skip(browserName === 'firefox', 'the direct preferences write does not always survive a Firefox reload');
  const labels = async () => Object.values((await snapshot(page)).controls).map(control => control.label).sort().join('|');
  const release = await hold(page, 'translations');
  await page.goto(gameURL());
  await screen(page, 'MainMenuScreen');
  await expect.poll(async () => (await labels()).length).toBeGreaterThan(0);
  const english = await labels();
  await chooseLanguage(page, 'de');
  await page.reload();
  await page.waitForTimeout(3000);
  expect((await snapshot(page)).screen).toBe('loading');
  release();
  await screen(page, 'MainMenuScreen');
  expect((await snapshot(page)).assets.translations).toBe('ready');
  await expect.poll(async () => (await labels()).length).toBeGreaterThan(0);
  expect(await labels()).not.toBe(english);
});
