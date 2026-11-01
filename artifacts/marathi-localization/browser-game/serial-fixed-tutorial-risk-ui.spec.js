// SPDX-License-Identifier: GPL-3.0-or-later
const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const {createHash} = require('node:crypto');
const {
  gameURL, clickMainMenu, clickControl, clickSettingsDone,
  clickCustomAIProfile, clickListRow, control, controlBox, rootBox, readBrowserFile,
} = require(path.resolve(__dirname, '../../../browser/tests/main-menu'));
const {hasDarkText, hasLightText} = require(path.resolve(__dirname, '../../../browser/tests/pixels'));

const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const label = (page, key) => page.evaluate(key => glob2Diagnostics.snapshot().controls[key]?.label, key);
const screen = (page, name) => expect.poll(async () => {
  const state = await snapshot(page);
  return state.screen + ' ' + state.screenClass;
}, {timeout: 120000}).toContain(name);
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

async function initialMenu(page) {
  // Touch first-run choices are part of the real game, not DOM stand-ins.
  await expect.poll(async () => {
    const state = await snapshot(page);
    const name = state.screen + ' ' + state.screenClass;
    if (name.includes('AudioPreferenceScreen') && state.controls['audio.silence']?.enabled)
      await clickControl(page, 'audio.silence');
    else if (name.includes('HandPreferenceScreen') && state.controls['hand.right']?.enabled)
      await clickControl(page, 'hand.right');
    else if (name.includes('IntroductionOfferScreen') && state.controls['intro/menu']?.enabled)
      await clickControl(page, 'intro/menu');
    else if (name.includes('IntroductionOfferScreen') && state.controls['intro/continue']?.enabled)
      await clickControl(page, 'intro/continue');
    return name.includes('MainMenuScreen');
  }, {timeout: 180000}).toBe(true);
}

async function choosePublishedPopupOption(page, text) {
  let options;
  await expect.poll(async () => {
    options = await page.evaluate(() => Object.entries(glob2Diagnostics.snapshot().controls)
      .filter(([key]) => /^popup\/\d+$/.test(key))
      .sort(([a], [b]) => Number(a.split('/')[1]) - Number(b.split('/')[1]))
      .map(([, value]) => value.label));
    return options.includes(text);
  }, {message: `popup option "${text}" is not available`}).toBe(true);
  // The selector was opened by a real click and owns keyboard focus. Read its
  // actual choices rather than assuming locale indices or popup wheel bounds.
  const index = options.indexOf(text);
  const fromEnd = index > options.length / 2;
  const canvas = page.locator('#canvas');
  await canvas.press(fromEnd ? 'End' : 'Home', {delay: 40});
  const steps = fromEnd ? options.length - 1 - index : index;
  for (let step = 0; step < steps; ++step)
    await canvas.press(fromEnd ? 'ArrowUp' : 'ArrowDown', {delay: 40});
  await canvas.press('Enter', {delay: 40});
}

async function playerCategory(page, currentLanguage) {
  await clickMainMenu(page, 'settings');
  await screen(page, 'SettingsScreen');
  if ((await snapshot(page)).controls['nav.5']) await clickControl(page, 'nav.5');
  else {
    await clickControl(page, 'nav.current');
    await choosePublishedPopupOption(page, currentLanguage === 'mr' ? 'भाषा आणि खेळाडू' : 'Language & player');
  }
  await control(page, 'player.language');
}

async function chooseLanguage(page, languageName) {
  await clickControl(page, 'player.language');
  await choosePublishedPopupOption(page, languageName);
  await expect.poll(() => label(page, 'player.language')).toBe(languageName);
  await clickSettingsDone(page);
  await screen(page, 'MainMenuScreen');
  await expect.poll(async () => (await snapshot(page)).persisting).toBe(false);
  await expect.poll(async () => (await snapshot(page)).persistence).toBe('persisted');
}

for (const device of [
  {name: 'desktop', viewport: {width: 1280, height: 900}, hasTouch: false},
  {name: 'phone', viewport: {width: 390, height: 844}, hasTouch: true},
]) test.describe(`Marathi game ${device.name}`, () => {
  test.use({viewport: device.viewport, hasTouch: device.hasTouch});
  for (const variant of ['serial', 'threaded']) test(`${variant} native catalog, selector and statistics render in Marathi`, async ({page, context, baseURL}, info) => {
    test.setTimeout(360000);
    const selected = {chromium: ['desktop','serial'], firefox: ['phone','serial'], webkit: ['desktop','serial']}[info.project.name];
    test.skip(device.name !== selected[0] || variant !== selected[1], 'Bounded current-risk refresh; prior full12 remains explicitly carried.');
    const evidence = path.join(__dirname, 'risk-ui-serial-fixed-tutorial', info.project.name, variant, device.name);
    fs.mkdirSync(evidence, {recursive: true});
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    // Each Playwright test has a fresh browser profile: only the online cookie
    // is seeded, with no saved native preferences or injected game state.
    await context.addCookies([{name: 'glob2_locale', value: 'mr', url: baseURL}]);
    const url = new URL(gameURL(), baseURL);
    url.searchParams.set('threads', variant);
    if (!url.searchParams.has('renderer')) url.searchParams.set('renderer', 'software');
    await page.goto(url.pathname + url.search);
    await initialMenu(page);
    expect((await snapshot(page)).executionMode).toBe(variant);
    await expect.poll(() => label(page, 'menu/1')).toBe('शिकवणी');
    expect((await snapshot(page)).assets.translations).toBe('ready');

    const registry = await page.evaluate(() => Glob2I18n.locales);
    expect(registry.length).toBe(39);
    expect(registry.map(entry => entry.code)).toEqual(expect.arrayContaining(['mr','ms']));
    // Read the actual packaged VFS bytes, independently of HTML's locale.
    const catalog = await readBrowserFile(page, '/data/texts.mr.txt');
    const font = await readBrowserFile(page, '/data/fonts/scripts/NotoSansDevanagari-Regular.ttf');
    const inventory = await readBrowserFile(page, '/data/texts.list.txt');
    expect(inventory.toString('utf8').split(/\r?\n/)).toContain('data/texts.mr.txt');
    expect(sha256(catalog)).toBe(sha256(fs.readFileSync(path.resolve(__dirname, '../../../data/texts.mr.txt'))));
    const fonts = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../../data/fonts/scripts/manifest.json'), 'utf8'));
    expect(sha256(font)).toBe(fonts.fonts.find(entry => entry.name === 'NotoSansDevanagari').sha256);
    const records = {variant, device: device.name, browser: info.project.name,
      catalogSha256: sha256(catalog), devanagariFontSha256: sha256(font), main: await snapshot(page)};
    await page.screenshot({path: path.join(evidence, 'main.png')});

    await playerCategory(page, 'mr');
    await expect.poll(() => label(page, 'player.language')).toBe('मराठी');
    const languageBox = await controlBox(page, 'player.language', {fullyVisible: true});
    // Exclude the button border; verify actual canvas ink in the selected name.
    expect(await hasDarkText(page, {x: languageBox.x + 8, y: languageBox.y + 5,
      width: Math.max(1, languageBox.width - 35), height: Math.max(1, languageBox.height - 10)}, 180)).toBe(true);
    records.settings = await snapshot(page);
    await page.screenshot({path: path.join(evidence, 'settings.png')});
    await clickSettingsDone(page);
    await screen(page, 'MainMenuScreen');

    // Current shared change: narrow statistics Overview occupies its own row.
    await page.reload(); await initialMenu(page);
    await clickMainMenu(page, 'tutorial');
    await screen(page, 'LessonCatalogScreen');
    await clickControl(page, 'tutorial/lesson/0');
    await screen(page, 'GameSessionScreen');
    await expect.poll(async () => (await snapshot(page)).tick, {timeout:120000}).toBeGreaterThan(25);
    records.fixedMapRoute='Actual published tutorial/lesson/0 (fixed authored map), no generated custom map or injected game state.';
    if (device.hasTouch) {
      await clickControl(page,'intro/collapse');
      await page.touchscreen.tap(162,820);
      const toolsFrame=(await snapshot(page)).frames;
      await expect.poll(async () => (await snapshot(page)).frames).toBeGreaterThan(toolsFrame+2);
      await page.screenshot({path:path.join(evidence,'statistics-tools.png')});
      await page.touchscreen.tap(75,445);
    } else await page.mouse.click(1196,163);
    await control(page,'stats/topic/0');
    expect(await label(page,'stats/topic/0')).toBe('आढावा');
    expect(await label(page,'stats/topic/2')).toBe('अर्थव्यवस्था');
    const statsFrame=(await snapshot(page)).frames;
    await expect.poll(async () => (await snapshot(page)).frames).toBeGreaterThan(statsFrame+2);
    records.statisticsOverview=await snapshot(page);
    records.statisticsTopicBounds={};
    for(let index=0;index<5;++index)
      records.statisticsTopicBounds[index]=await controlBox(page,'stats/topic/'+index,{fullyVisible:true});
    await page.screenshot({path:path.join(evidence,'statistics-overview.png')});
    records.currentRegistry=registry;
    records.errors = errors;
    fs.writeFileSync(path.join(evidence, 'evidence.json'), JSON.stringify(records, null, 2) + '\n');
    expect(errors).toEqual([]);
  });
});
