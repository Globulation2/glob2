// The launcher, native catalog, and DOM text input must select the same locale.
const {test, expect} = require('@playwright/test');
const {readFileSync} = require('node:fs');
const {resolve} = require('node:path');
const {gameURL, control, clickControl, clickByLabel, clickMainMenu, clickSettingsDone,
  waitForMainMenu} = require('./main-menu');
const {clickCreateMap} = require('./editor-controls');
const root = resolve(__dirname, '../..');
const lines = readFileSync(resolve(root, 'data/texts.fil.txt'), 'utf8').split('\n');
const native = new Map();
for (let index = 0; index + 1 < lines.length; index += 2) native.set(lines[index], lines[index + 1]);
const online = JSON.parse(readFileSync(resolve(root, 'platform/packages/i18n/locales/fil.json'), 'utf8'));
const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await snapshot(page)).screen).toContain(name);

for (const mode of ['serial', 'threaded']) for (const phone of [false, true]) {
  test.describe(`Filipino ${mode} ${phone ? 'phone' : 'desktop'}`, () => {
    test.use({viewport: phone ? {width: 390, height: 844} : {width: 1280, height: 860}, hasTouch: phone});
    test('launcher, settings input, and editor render Filipino', async ({page, context}, info) => {
      test.setTimeout(240000);
      const url = new URL(gameURL(), String(info.project.use.baseURL || 'http://127.0.0.1:8770'));
      url.searchParams.set('threads', mode);
      await context.addCookies([{name: 'glob2_locale', value: 'fil', url: url.origin}]);
      await page.goto(url.pathname + url.search);
      await expect.poll(async () => {
        const state = await snapshot(page);
        if (state.controls['audio.silence']?.enabled) await clickControl(page, 'audio.silence');
        else if (state.controls['hand.right']?.enabled) await clickControl(page, 'hand.right');
        else if (state.controls['intro/menu']?.enabled) await clickControl(page, 'intro/menu');
        else if (state.controls['intro/continue']?.enabled) await clickControl(page, 'intro/continue');
        return state.screen.includes('MainMenuScreen');
      }, {timeout: 120000}).toBe(true);
      const execution = await page.evaluate(() => Module.executionMode);
      test.skip(info.project.name === 'webkit' && mode === 'threaded' && execution === 'serial',
        'This WebKit host does not support the threaded runtime capability probe');
      expect(execution).toBe(mode);
      await expect(page.locator('html')).toHaveAttribute('lang', 'fil');
      await expect(page.locator('#loading-language')).toHaveValue('fil');
      expect((await control(page, 'menu/3')).label).toBe(native.get('[custom game]'));
      await page.screenshot({path: info.outputPath('fil-main.png')});

      await clickMainMenu(page, 'settings');
      await screen(page, 'SettingsScreen');
      await clickControl(page, 'display.textsize');
      await clickControl(page, 'popup/2'); // Exercise the player's 150% text setting.
      await expect.poll(async () => (await control(page, 'display.textsize')).label).toMatch(/150\s*%/);
      await expect.poll(async () => Boolean((await snapshot(page)).controls['popup/2'])).toBe(false);
      await page.screenshot({path: info.outputPath('fil-settings-large-text.png')});
      if ((await snapshot(page)).controls['nav.current']) {
        await clickControl(page, 'nav.current');
        await clickByLabel(page, /^popup\/\d+$/, native.get('[settings Language & player]'));
      } else await clickControl(page, 'nav.5');
      await clickControl(page, 'player.name');
      const field = page.getByRole('textbox', {name: online['Game text field'], exact: true});
      await expect(field).toBeVisible();
      await field.fill('Ligaya ñ');
      await expect(field).toHaveValue('Ligaya ñ');
      await page.screenshot({path: info.outputPath('fil-settings-input.png')});
      await field.press('Enter');
      await clickSettingsDone(page);
      await waitForMainMenu(page);
      await clickMainMenu(page, 'editor');
      await screen(page, 'EditorMainMenu');
      await clickControl(page, 'new-map');
      await screen(page, 'NewMapScreen');
      await page.screenshot({path: info.outputPath('fil-new-map.png')});
      await clickCreateMap(page);
      await screen(page, 'MapEditorScreen');
      if (phone) await control(page, 'tray/mode/2');
      await page.screenshot({path: info.outputPath('fil-editor.png')});
      expect(await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth))
        .toBeLessThanOrEqual(1);
    });
  });
}
