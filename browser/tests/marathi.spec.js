// SPDX-License-Identifier: GPL-3.0-or-later
const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const {createHash} = require('node:crypto');
const {
  gameURL, clickMainMenu, clickControl, clickSettingsDone,
  clickCustomAIProfile, clickListRow, control, controlBox, rootBox, readBrowserFile,
} = require('./main-menu');
const {hasDarkText, hasLightText} = require('./pixels');

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
  for (const variant of ['serial', 'threaded']) test(`${variant} native catalog, settings and tutorial render in Marathi`, async ({page, context, baseURL}, info) => {
    test.setTimeout(360000);
    const evidence = path.resolve(__dirname, '../../artifacts/marathi-localization/browser-game', info.project.name, variant, device.name);
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

    // Read the actual packaged VFS bytes, independently of HTML's locale.
    const catalog = await readBrowserFile(page, '/data/texts.mr.txt');
    const font = await readBrowserFile(page, '/data/fonts/scripts/NotoSansDevanagari-Regular.ttf');
    const inventory = await readBrowserFile(page, '/data/texts.list.txt');
    expect(inventory.toString('utf8').split(/\r?\n/)).toContain('data/texts.mr.txt');
    expect(sha256(catalog)).toBe(sha256(fs.readFileSync(path.resolve(__dirname, '../../data/texts.mr.txt'))));
    const fonts = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../data/fonts/scripts/manifest.json'), 'utf8'));
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

    // The actual AI dialog lays out long translated paragraphs and scrolls
    // them, beyond the short labels and font probes used at startup.
    await clickMainMenu(page, 'custom');
    await screen(page, 'CustomGameScreen');
    await clickControl(page, 'tab/1');
    await clickCustomAIProfile(page, 1);
    await screen(page, 'CustomGameChoiceScreen');
    if ((await snapshot(page)).controls['profile/select']) {
      await clickControl(page, 'profile/select');
      await choosePublishedPopupOption(page, 'Maxima');
    } else await clickListRow(page, 'profile/list', /Maxima/);
    await expect.poll(() => label(page, 'profile/use')).toBe('वापरा Maxima');
    records.aiProfileTop = await snapshot(page);
    await page.screenshot({path: path.join(evidence, 'ai-profile-top.png')});
    // Scroll regions are not interactive diagnostic targets. Aim the wheel
    // inside the actual published dialog, beyond its optional profile list.
    const dialog = await rootBox(page, 'profile/use');
    const list = records.aiProfileTop.controls['profile/list']
      ? await controlBox(page, 'profile/list') : null;
    const left = list ? list.x + list.width : dialog.x;
    const center = {x: (left + dialog.x + dialog.width) / 2,
      y: dialog.y + dialog.height / 2};
    // This opaque central text crop excludes the animated menu background,
    // footer and selector, so a change proves that the prose actually scrolled.
    const crop = {x: Math.round(center.x - dialog.width / 8),
      y: Math.round(center.y - dialog.height / 8),
      width: Math.round(dialog.width / 4), height: Math.round(dialog.height / 4)};
    const beforeScroll = sha256(await page.screenshot({clip: crop}));
    await page.mouse.move(center.x, center.y);
    for (let step = 0; step < 3; ++step) await page.mouse.wheel(0, 1600);
    await expect.poll(async () => sha256(await page.screenshot({clip: crop})),
      {message: 'AI profile prose did not scroll'}).not.toBe(beforeScroll);
    records.aiProfileScroll = {crop, beforeSha256: beforeScroll,
      afterSha256: sha256(await page.screenshot({clip: crop}))};
    records.aiProfileBottom = await snapshot(page);
    await page.screenshot({path: path.join(evidence, 'ai-profile-bottom.png')});
    await clickControl(page, 'profile/back');
    await screen(page, 'CustomGameScreen');
    // Back exits from the first setup step on both touch and desktop.
    await clickControl(page, 'tab/0');
    await control(page, 'map/mode/0');
    await clickControl(page, 'back');
    await screen(page, 'MainMenuScreen');

    if (device.name === 'desktop') {
      // Save through the real native settings UI. An explicit saved language
      // must beat a conflicting web cookie after IndexedDB restore.
      await playerCategory(page, 'mr');
      await chooseLanguage(page, 'English');
      await context.addCookies([{name: 'glob2_locale', value: 'mr', url: baseURL}]);
      await page.reload();
      await initialMenu(page);
      expect((await snapshot(page)).executionMode).toBe(variant);
      await expect.poll(() => label(page, 'menu/1')).toBe('Tutorial');
      records.savedEnglishBeatsMarathiCookie = await snapshot(page);
      await playerCategory(page, 'en');
      await chooseLanguage(page, 'मराठी');
      await context.addCookies([{name: 'glob2_locale', value: 'en', url: baseURL}]);
      await page.reload();
      await initialMenu(page);
      await expect.poll(() => label(page, 'menu/1')).toBe('शिकवणी');
      records.savedMarathiBeatsEnglishCookie = await snapshot(page);
    }

    await clickMainMenu(page, 'tutorial');
    await screen(page, 'LessonCatalogScreen');
    await expect.poll(() => label(page, 'tutorial/lesson/0')).toBe('परिचय');
    records.catalog = await snapshot(page);
    await page.screenshot({path: path.join(evidence, 'lessons.png')});
    await clickControl(page, 'tutorial/lesson/0');
    await screen(page, 'GameSessionScreen');
    await expect.poll(async () => (await snapshot(page)).tick, {timeout: 120000}).toBeGreaterThan(25);
    await expect.poll(() => label(page, 'intro/help')).toBe('मदत / का?');
    await control(page, 'intro/help');
    records.lesson = await snapshot(page);
    await page.screenshot({path: path.join(evidence, 'lesson.png')});
    await clickControl(page, 'intro/help');
    // Help retains the instruction even if an inactivity hint has appeared.
    if (device.hasTouch) {
      // Instruction paragraphs are intentionally absent from interactive-only
      // diagnostics. Verify real glyph ink below the heading/actions; exact
      // Marathi instruction/help content is checked in the saved image review.
      const card = await rootBox(page, 'intro/help');
      const help = await controlBox(page, 'intro/help');
      const top = Math.ceil(help.y + help.height + 4);
      const body = {x: Math.ceil(card.x + 4), y: top,
        width: Math.floor(card.width - 8), height: Math.floor(card.y + card.height - top - 4)};
      expect(body.height).toBeGreaterThan(20);
      expect(body.width).toBeGreaterThan(20);
      expect(await hasLightText(page, body, 150)).toBe(true);
      records.phoneHelpBody = {clip: body, minimumChannel: 150, renderedGlyphInk: true,
        exactTextCoverage: 'Manual instruction/help image review; paragraph text is not a published diagnostic label.'};
    }
    records.lessonHelp = await snapshot(page);
    await page.screenshot({path: path.join(evidence, 'lesson-help.png')});
    records.errors = errors;
    fs.writeFileSync(path.join(evidence, 'evidence.json'), JSON.stringify(records, null, 2) + '\n');
    expect(errors).toEqual([]);
  });
});
