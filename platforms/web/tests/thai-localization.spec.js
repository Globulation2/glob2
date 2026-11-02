const {test, expect} = require('@playwright/test');
const {createHash} = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {waitForMainMenu, gameURL, clickMainMenu, clickControl, clickSettingsDone, tapControl, clickByLabel, readBrowserFile, clickListRow} = require('./main-menu');
const thai = require('../../services/online/packages/i18n/locales/th.json');

test.use({locale: 'th-TH'});

const shippedThaiFiles = ['data/texts.th.txt', 'data/fonts/scripts/NotoSansThai-Regular.ttf'];
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
async function expectShippedThaiAssets(page) {
  for (const relative of shippedThaiFiles) {
    const actual = await readBrowserFile(page, '/' + relative);
    expect(sha256(actual), relative).toBe(sha256(fs.readFileSync(path.resolve(__dirname, '../../../content', relative))));
  }
}


// Exercise actual CampaignEditor and Campaign save/load, without a shipped fixture.
async function expectThaiCampaignEditor(page, info, variant, touch) {
  if (touch) {
    await page.setViewportSize({width: 390, height: 844});
    await expect.poll(() => page.evaluate(() => [Module.presentationMetrics.width, Module.presentationMetrics.height]))
      .toEqual([390, 844]);
  }
  const screen = name => expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen)).toContain(name);
  const activate = key => touch ? tapControl(page, key) : clickControl(page, key);
  const lines = fs.readFileSync(path.resolve(__dirname, '../../../content/data/texts.th.txt'), 'utf8').split(/\r?\n/);
  const index = lines.indexOf('[new campaign]');
  expect(index).toBeGreaterThanOrEqual(0);
  const expected = lines[index + 1].replace(/\u200b/g, '');
  await clickMainMenu(page, 'editor');
  await screen('EditorMainMenu');
  await activate('new-campaign');
  await screen('CampaignEditor');
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().controls.name?.label)).toBe(expected);
  await page.screenshot({path: info.outputPath(`${variant}-thai-${touch ? 'touch-phone' : 'desktop'}-campaign-new.png`)});
  await activate('name');
  const field = page.locator('input[data-i18n-aria="Game text field"]');
  await expect(field).toHaveValue(expected);
  expect(await field.inputValue()).not.toContain('\u200b');
  const authored = 'Author Campaign QA';
  if (touch) await field.tap(); else await field.click();
  await field.press('ControlOrMeta+A');
  await field.press('Delete');
  await field.pressSequentially(authored);
  await expect(field).toHaveValue(authored);
  await activate('ok');
  await screen('EditorMainMenu');
  const digest = () => page.evaluate(() => glob2Diagnostics.campaignDefinitionDigest('Author_Campaign_QA.txt'));
  const before = await digest();
  expect(before?.size).toBeGreaterThan(0);
  await activate('load-campaign');
  await screen('CampaignSelectorScreen');
  await clickListRow(page, 'campaigns', 'Author_Campaign_QA');
  await activate('ok');
  await screen('CampaignEditor');
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().controls.name?.label)).toBe(authored);
  await activate('name');
  await expect(field).toHaveValue(authored);
  await page.screenshot({path: info.outputPath(`${variant}-thai-${touch ? 'touch-phone' : 'desktop'}-campaign-loaded.png`)});
  const after = await digest();
  expect(after).toEqual(before);
  await info.attach(`${variant}-campaign-name-preservation`, {
    body: Buffer.from(JSON.stringify({freshTitle: expected, authoredTitle: authored, noSoftHints: true,
      before, after, scope: 'Real UI authoring/save/load in isolated browser profile. Authored ASCII title remains raw; no fixture or provider request shipped.'}, null, 2)),
    contentType: 'application/json',
  });
  await activate('cancel');
  await screen('EditorMainMenu');
  await page.locator('#canvas').press('Escape');
  await waitForMainMenu(page);
}

for (const variant of ['serial', 'threaded']) {
  test(`${variant} Thai startup, composed input and refresh use the shipped catalog`, async ({page, context}, info) => {
    // Two full software-rendered startups plus campaign authoring can exceed three minutes.
    test.setTimeout(420000);
    const url = new URL(gameURL(), String(info.project.use.baseURL || 'http://127.0.0.1:8770'));
    url.searchParams.set('threads', variant);
    url.searchParams.set('renderer', 'software');
    await context.addCookies([{name: 'glob2_locale', value: 'th', url: url.origin}]);
    await page.goto(url.href);
    await waitForMainMenu(page);
    expect(await page.evaluate(() => Module.executionMode)).toBe(variant);
    await expect(page.locator('html')).toHaveAttribute('lang', 'th');
    await expect(page.locator('#loading-language')).toHaveValue('th');
    const state = await page.evaluate(() => ({
      labels: Object.values(glob2Diagnostics.snapshot().controls).map(control => control.label),
      translations: glob2Diagnostics.snapshot().assets.translations,
    }));
    expect(state.labels.some(label => /[\u0e00-\u0e7f]/.test(label))).toBe(true);
    expect(state.translations).toBe('ready');
    await expectShippedThaiAssets(page);
    await page.screenshot({path: info.outputPath(`${variant}-thai-menu.png`)});
    await page.setViewportSize({width: 390, height: 844});
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().width)).toBe(390);
    await page.screenshot({path: info.outputPath(`${variant}-thai-narrow-desktop-menu.png`)});
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
      // Stacked Thai marks within Settings' existing 32-byte UTF-8 name limit.
      element.value = 'ผู้เล่น 123';
      element.dispatchEvent(new InputEvent('input', {bubbles: true, data: element.value, isComposing: true}));
      element.dispatchEvent(new CompositionEvent('compositionend', {bubbles: true, data: element.value}));
      element.dispatchEvent(new InputEvent('input', {bubbles: true, data: element.value}));
    });
    await expect(field).toHaveValue('ผู้เล่น 123');
    await expect.poll(() => page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'}))).toContain('username=ผู้เล่น 123');
    await page.screenshot({path: info.outputPath(`${variant}-thai-player-input.png`)});
    await clickControl(page, 'player.language');
    await clickByLabel(page, /^popup\/\d+$/, 'English');
    await expect.poll(() => page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'})))
      .toMatch(/^language=en$/m);
    await clickSettingsDone(page);
    await waitForMainMenu(page);
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().controls['menu/6']?.label)).toBe('Settings');
    await page.screenshot({path: info.outputPath(`${variant}-english-after-thai.png`)});
    await clickMainMenu(page, 'settings');
    await clickControl(page, 'nav.5');
    await clickControl(page, 'player.language');
    await clickByLabel(page, /^popup\/\d+$/, 'ไทย');
    await expect.poll(() => page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'})))
      .toMatch(/^language=th$/m);
    await clickSettingsDone(page);
    await waitForMainMenu(page);
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().controls['menu/6']?.label.replace(/\u200b/g, '')))
      .toBe('การตั้งค่า');
    await page.evaluate(() => Module.storage.flush());
    await page.reload();
    await waitForMainMenu(page, {timeout: 180000});
    await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().controls['menu/6']?.label.replace(/\u200b/g, '')))
      .toBe('การตั้งค่า');
    expect(await page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'}))).toContain('username=ผู้เล่น 123');
    expect(await page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'}))).toMatch(/^language=th$/m);
    await expect(page.locator('html')).toHaveAttribute('lang', 'th');
    await expectThaiCampaignEditor(page, info, variant, false);
  });
}

test.describe('Thai touch phones', () => {
  test.use({viewport: {width: 390, height: 844}, hasTouch: true, deviceScaleFactor: 2});
  for (const variant of ['serial', 'threaded']) {
    test(`${variant} Thai touch setup, input and rotation retain the locale`, async ({page, context}, info) => {
      // Two full software-rendered startups plus campaign authoring can exceed three minutes.
    test.setTimeout(420000);
      const url = new URL(gameURL(), String(info.project.use.baseURL || 'http://127.0.0.1:8770'));
      url.searchParams.set('threads', variant);
      url.searchParams.set('renderer', 'software');
      await context.addCookies([{name: 'glob2_locale', value: 'th', url: url.origin}]);
      await page.goto(url.href);
      // Firefox can lose emulated density during the COOP process replacement.
      await page.setViewportSize(page.viewportSize());
      expect(await page.evaluate(() => devicePixelRatio)).toBe(2);
      await expect.poll(async () => {
        const state = await page.evaluate(() => glob2Diagnostics.snapshot());
        const name = state.screen + ' ' + state.screenClass;
        if (name.includes('AudioPreferenceScreen') && state.controls['audio.silence']?.enabled)
          await tapControl(page, 'audio.silence');
        else if (name.includes('HandPreferenceScreen') && state.controls['hand.right']?.enabled)
          await tapControl(page, 'hand.right');
        else if (name.includes('IntroductionOfferScreen') && state.controls['intro/menu']?.enabled)
          await tapControl(page, 'intro/menu');
        return name.includes('MainMenuScreen');
      }, {timeout: 120000}).toBe(true);
      expect(await page.evaluate(() => Module.executionMode)).toBe(variant);
      expect(await page.evaluate(() => Module.presentationMetrics.touch)).toBe(true);
      await expect(page.locator('html')).toHaveAttribute('lang', 'th');
      await expect.poll(() => page.evaluate(() => {
        const state = glob2Diagnostics.snapshot();
        return [state.width, state.height];
      })).toEqual([780, 1688]);
      expect(await page.evaluate(() => [Module.presentationMetrics.width, Module.presentationMetrics.height]))
        .toEqual([390, 844]);
      expect(await page.evaluate(() => Object.values(glob2Diagnostics.snapshot().controls)
        .some(control => /[\u0e00-\u0e7f]/.test(control.label)))).toBe(true);
      await expectShippedThaiAssets(page);
      await page.screenshot({path: info.outputPath(`${variant}-thai-touch-phone-menu.png`)});
      await clickMainMenu(page, 'settings');
      await page.screenshot({path: info.outputPath(`${variant}-thai-touch-phone-settings.png`)});
      await tapControl(page, 'nav.current');
      // The touch picker omits Controls: Player follows the first four categories.
      await clickControl(page, 'popup/4');
      await tapControl(page, 'player.name');
      const field = page.locator('input[data-i18n-aria="Game text field"]');
      await expect(field).toHaveAttribute('aria-label', thai['Game text field']);
      await field.fill('ผู้เล่น 123');
      await expect(field).toHaveValue('ผู้เล่น 123');
      await expect.poll(() => page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'})))
        .toContain('username=ผู้เล่น 123');
      await page.screenshot({path: info.outputPath(`${variant}-thai-touch-phone-input.png`)});
      await page.setViewportSize({width: 844, height: 390});
      await expect.poll(() => page.evaluate(() => {
        const state = glob2Diagnostics.snapshot();
        return [state.width, state.height];
      })).toEqual([1688, 780]);
      expect(await page.evaluate(() => [Module.presentationMetrics.width, Module.presentationMetrics.height]))
        .toEqual([844, 390]);
      await expect(field).toHaveValue('ผู้เล่น 123');
      await expect.poll(async () => {
        const box = await field.boundingBox();
        return Boolean(box && box.x >= 0 && box.y >= 0 && box.x + box.width <= 844.5 && box.y + box.height <= 390.5);
      }).toBe(true);
      await page.screenshot({path: info.outputPath(`${variant}-thai-touch-phone-rotation.png`)});
      await clickSettingsDone(page);
      await waitForMainMenu(page);
      await page.evaluate(() => Module.storage.flush());
      await page.reload();
      await page.setViewportSize(page.viewportSize());
      expect(await page.evaluate(() => devicePixelRatio)).toBe(2);
      await waitForMainMenu(page, {timeout: 180000});
      await expect(page.locator('html')).toHaveAttribute('lang', 'th');
      expect(await page.evaluate(() => Object.values(glob2Diagnostics.snapshot().controls)
        .some(control => /[\u0e00-\u0e7f]/.test(control.label)))).toBe(true);
      expect(await page.evaluate(() => FS.readFile('/home/web_user/.glob2/preferences.txt', {encoding: 'utf8'})))
        .toContain('username=ผู้เล่น 123');
      await expectThaiCampaignEditor(page, info, variant, true);
    });
  }
});
