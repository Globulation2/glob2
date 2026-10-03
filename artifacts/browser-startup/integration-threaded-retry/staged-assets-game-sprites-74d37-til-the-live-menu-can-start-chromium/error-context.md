# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: staged-assets.spec.js >> game sprites stay under download progress until the live menu can start
- Location: browser/tests/staged-assets.spec.js:35:1

# Error details

```
Error: expect(received).toBe(expected) // Object.is equality

Expected: false
Received: true

Call Log:
- Timeout 5000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [aria-hidden]:
    - strong: Globulation 2
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 25 of 25 MB
    - paragraph: The first visit downloads the game once. Later visits start from this device's copy.
  - generic "Globulation 2" [active] [ref=e2]
```

# Test source

```ts
  1   | const {test, expect} = require('@playwright/test');
  2   | const {gameURL, clickMainMenu, clickCustomGameStart} = require('./main-menu');
  3   | 
  4   | // Data packages (scons/web_assets.py): startup sprites, the full font and menu
  5   | // music. Native builds load all of it at startup.
  6   | const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
  7   | const screen = (page, name, timeout = 60000) =>
  8   |   expect.poll(async () => (await snapshot(page)).screen, {timeout}).toContain(name);
  9   | // Hold a package's download until the returned function is called.
  10  | async function hold(page, name) {
  11  |   let release;
  12  |   const gate = new Promise(resolve => { release = resolve; });
  13  |   await page.route(`**/assets/${name}*.data`, async route => { await gate; await route.continue(); });
  14  |   return release;
  15  | }
  16  | 
  17  | // Choose the interface language for the next visit, as the settings screen would.
  18  | // The game may write its preferences just after the menu appears, so check that
  19  | // the choice is what reached storage.
  20  | async function chooseLanguage(page, code) {
  21  |   await expect.poll(async () => page.evaluate(async code => {
  22  |     const path = '/home/web_user/.glob2/preferences.txt';
  23  |     const read = () => FS.analyzePath(path).exists ? FS.readFile(path, {encoding: 'utf8'}) : '';
  24  |     if (!read().includes('language=' + code + '\n')) {
  25  |       FS.writeFile(path, read().replace(/^language=.*\n?/m, '') + 'language=' + code + '\n');
  26  |       await Module.storage.flush();
  27  |     }
  28  |     await new Promise(resolve => setTimeout(resolve, 500));
  29  |     return read().includes('language=' + code + '\n');
  30  |   }, code)).toBe(true);
  31  |   // Let the browser finish committing IndexedDB before the page goes away.
  32  |   await page.waitForTimeout(1500);
  33  | }
  34  | 
  35  | test('game sprites stay under download progress until the live menu can start', async ({page}, info) => {
  36  |   const errors = [];
  37  |   page.on('pageerror', error => errors.push(String(error)));
  38  |   page.on('console', message => { if (/Can't load sprite|abort/i.test(message.text())) errors.push(message.text()); });
  39  |   const release = await hold(page, 'game');
  40  |   const releaseMusic = await hold(page, 'menu-music');
  41  |   await page.goto(gameURL());
  42  |   await expect.poll(async () => (await snapshot(page)).assets.core).toBe('ready');
  43  |   await expect(page.locator('#loading-status')).toHaveText('Downloading the game…');
  44  |   await page.waitForTimeout(3000);
  45  |   expect((await snapshot(page)).screen).toBe('loading');
  46  |   expect((await snapshot(page)).assets.game).toBe('downloading');
  47  |   expect(await page.locator('body').evaluate(body => body.classList.contains('ready'))).toBe(false);
  48  |   release();
  49  |   await screen(page, 'MainMenuScreen');
  50  |   await expect(page.locator('body')).toHaveClass(/ready/);
  51  |   expect((await snapshot(page)).assets.game).toBe('ready');
  52  |   // This region contains only colony terrain and units. It must animate as
  53  |   // soon as the menu appears, even while every later package is still held.
  54  |   const firstLoop = (await snapshot(page)).loop;
  55  |   await expect.poll(async () => (await snapshot(page)).loop).toBeGreaterThan(firstLoop + 1);
  56  |   const clip = {x:800, y:200, width:350, height:500};
  57  |   const first = await page.screenshot({clip, animations:'disabled', path:info.outputPath('colony-first.png')});
  58  |   await expect.poll(async () => (await page.screenshot({clip, animations:'disabled'})).equals(first),
> 59  |     {timeout:5000}).toBe(false);
      |                     ^ Error: expect(received).toBe(expected) // Object.is equality
  60  |   await page.screenshot({clip, animations:'disabled', path:info.outputPath('colony-moving.png')});
  61  |   releaseMusic();
  62  |   await clickMainMenu(page, 'custom');
  63  |   await screen(page, 'CustomGameScreen');
  64  |   await clickCustomGameStart(page);
  65  |   await expect.poll(async () => (await snapshot(page)).tick, {timeout: 60000}).toBeGreaterThan(25);
  66  |   expect(errors).toEqual([]);
  67  | });
  68  | 
  69  | test('later packages arrive in the background and replace the core font', async ({page}) => {
  70  |   await page.goto(gameURL());
  71  |   await screen(page, 'MainMenuScreen');
  72  |   for (const name of ['game', 'menu-music', 'font-cjk', 'translations'])
  73  |     await expect.poll(async () => (await snapshot(page)).assets[name], {timeout: 120000}).toBe('ready');
  74  |   // The full font now has the CJK outlines the core copy leaves out.
  75  |   const size = await page.evaluate(() => FS.stat('/data/fonts/sans.ttf').size);
  76  |   expect(size).toBeGreaterThan(4e6);
  77  |   expect(await page.evaluate(() => FS.analyzePath('/data/zik/menu.ogg').exists)).toBe(true);
  78  |   // The reloaded string table still drives the menu.
  79  |   await clickMainMenu(page, 'custom');
  80  |   await screen(page, 'CustomGameScreen');
  81  | });
  82  | 
  83  | test('a Chinese interface downloads the full font before the game starts', async ({page, browserName}) => {
  84  |   // Firefox under automation occasionally restores the profile from before the
  85  |   // test's direct preferences write (about 1 run in 10); the setting screen's own
  86  |   // save path is not involved here.
  87  |   test.skip(browserName === 'firefox', 'the direct preferences write does not always survive a Firefox reload');
  88  |   // Keep the font off this device so the second visit has to download it.
  89  |   const release = await hold(page, 'font-cjk');
  90  |   await page.goto(gameURL());
  91  |   await screen(page, 'MainMenuScreen');
  92  |   await chooseLanguage(page, 'zh-cn');
  93  |   await page.reload();
  94  |   await expect(page.locator('#loading-status')).toHaveText('Downloading the game…');
  95  |   await page.waitForTimeout(3000);
  96  |   expect((await snapshot(page)).screen).toBe('loading');
  97  |   release();
  98  |   await screen(page, 'MainMenuScreen');
  99  |   expect((await snapshot(page)).assets['font-cjk']).toBe('ready');
  100 |   expect(await page.evaluate(() => FS.stat('/data/fonts/sans.ttf').size)).toBeGreaterThan(4e6);
  101 | });
  102 | 
  103 | test('an interface in another language downloads its translations before the game starts', async ({page, browserName}) => {
  104 |   // Firefox under automation occasionally restores the profile from before the
  105 |   // test's direct preferences write (about 1 run in 10); the setting screen's own
  106 |   // save path is not involved here.
  107 |   test.skip(browserName === 'firefox', 'the direct preferences write does not always survive a Firefox reload');
  108 |   const labels = async () => Object.values((await snapshot(page)).controls).map(control => control.label).sort().join('|');
  109 |   const release = await hold(page, 'translations');
  110 |   await page.goto(gameURL());
  111 |   await screen(page, 'MainMenuScreen');
  112 |   await expect.poll(async () => (await labels()).length).toBeGreaterThan(0);
  113 |   const english = await labels();
  114 |   await chooseLanguage(page, 'de');
  115 |   await page.reload();
  116 |   await page.waitForTimeout(3000);
  117 |   expect((await snapshot(page)).screen).toBe('loading');
  118 |   release();
  119 |   await screen(page, 'MainMenuScreen');
  120 |   expect((await snapshot(page)).assets.translations).toBe('ready');
  121 |   await expect.poll(async () => (await labels()).length).toBeGreaterThan(0);
  122 |   expect(await labels()).not.toBe(english);
  123 | });
  124 | 
```