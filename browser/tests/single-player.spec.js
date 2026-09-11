const {gameURL,clickMainMenu,clickSettingsDone,clickCustomGameStart}=require('./main-menu');
const {test, expect} = require('@playwright/test');

const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const click = (page, x, y) => page.locator('#canvas').click({position:{x,y}, delay:80});
const menu = (page, x, y) => click(page, x + 280, y + 210);

// Hold the first scheduled turn after entering a loader. The real Escape event
// can then reach a pending job without racing a fast machine's completed load.
// This injects the browser timer boundary; gameplay and diagnostics stay unchanged.
async function holdLoader(page, name) {
  await page.evaluate(name => {
    const schedule = window.setTimeout;
    window.setTimeout = function(callback, delay, ...args) {
      if (glob2Diagnostics.snapshot().screen.includes(name)) {
        window.setTimeout = schedule;
        window.releaseLoaderTurn = () => {
          delete window.releaseLoaderTurn;
          schedule(callback, delay, ...args);
        };
        return 0;
      }
      return schedule(callback, delay, ...args);
    };
  }, name);
}
async function cancelHeldLoader(page, name) {
  await screen(page, name);
  await expect.poll(() => page.evaluate(() => typeof releaseLoaderTurn)).toBe('function');
  await page.locator('#canvas').press('Escape', {delay:80});
  await page.evaluate(() => releaseLoaderTurn());
}

test.beforeEach(async ({page}, info) => {
  // Pin the editor's wall-time seed before runtime initialization, matching
  // the native generation fixture. Animation and cooperative timers remain real.
  if (info.title.startsWith('map generation can')) await page.clock.setFixedTime(12345 * 1000);
  await page.goto(gameURL());
  await screen(page, 'MainMenuScreen');
});

test('starts with a full-page game and restored local storage', async ({page}) => {
  expect(await state(page)).toMatchObject({width:1200, height:900, restore:'ready'});
  await expect(page.locator('button')).toHaveCount(0);
  await expect(page.locator('#loading')).toHaveAttribute('aria-hidden','true');
  expect(await page.locator('#canvas').boundingBox()).toMatchObject({x:0,y:0,width:1200,height:900});
});

test('application host returns from settings and credits and shuts down cleanly', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'settings');
  await screen(page, 'SettingsScreen');
  await clickSettingsDone(page);
  await screen(page, 'MainMenuScreen');
  await clickMainMenu(page, 'credits');
  await screen(page, 'CreditScreen');
  await page.locator('#canvas').press('Escape');
  await screen(page, 'MainMenuScreen');
  await clickMainMenu(page, 'quit');
  await screen(page, 'exited');
  expect(errors).toEqual([]);
});

test('campaign selector returns to its suspended parent and can reopen', async ({page}) => {
  await clickMainMenu(page, 'campaign');
  await screen(page, 'CampaignMainMenu');
  for (let attempt = 0; attempt < 2; ++attempt) {
    await menu(page, 320, 90);
    await screen(page, 'CampaignSelectorScreen');
    await page.locator('#canvas').press('Escape');
    await screen(page, 'CampaignMainMenu');
  }
  await page.locator('#canvas').press('Escape');
  await screen(page, 'MainMenuScreen');
});

test('tutorial sessions quit through the end screen and can restart', async ({page}) => {
  await clickMainMenu(page, 'tutorial');
  await screen(page, 'CampaignMenuScreen');
  for (let attempt = 0; attempt < 2; ++attempt) {
    await menu(page, 100, 60);
    await menu(page, 160, 450);
    await screen(page, 'match');
    await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
    await page.locator('#canvas').press('Escape');
    await click(page, 600, 500);
    await screen(page, 'EndGameScreen');
    await page.locator('#canvas').press('Enter');
    await screen(page, 'CampaignMenuScreen');
  }
});

test('game rules and AI descriptions return to setup, and a finished game returns there too', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
  // The lobby redesign (#237) folded "other options" into inline Game Rules
  // rows - no separate screen to navigate to and back from anymore.
  await click(page, 969, 35); // Game Rules tab.
  await click(page, 452, 133); // "Quick clash" tile.
  // The AI profile picker (Players & Teams tab, a colony's Info button) is a
  // screen CustomGameScreen pushes - see CustomGameScreen::showAIProfile.
  // It must actually be pushed, not blocking-executed: the browser host has
  // no Asyncify, so the old choose()/Screen::execute() pattern this replaced
  // threw and froze the page (docs/browser/adr-003-screen-execution.md).
  await click(page, 593, 35); // Players & Teams tab.
  await click(page, 1113, 438); // Colony 4's Info button (AI by default).
  await screen(page, 'CustomGameChoiceScreen');
  await click(page, 213, 201); // Warrush row.
  await click(page, 728, 862); // "Use Warrush".
  await screen(page, 'CustomGameScreen');
  await clickCustomGameStart(page); // A fresh profile has a valid premade map preselected.
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('Escape');
  await click(page, 600, 500);
  await screen(page, 'EndGameScreen');
  await page.locator('#canvas').press('Enter');
  await screen(page, 'CustomGameScreen');
  await page.locator('#canvas').press('Escape');
  await screen(page, 'MainMenuScreen');
  expect(errors).toEqual([]);
});

test('custom match pauses, persists and resumes after reload', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
  await clickCustomGameStart(page); // A fresh profile has a valid premade map preselected.
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('p', {delay:80});
  await expect.poll(async () => (await state(page)).paused).toBe(true);
  const paused = await state(page);
  await expect.poll(async () => (await state(page)).frames).toBeGreaterThan(paused.frames + 10);
  expect((await state(page)).tick).toBe(paused.tick);

  await page.locator('#canvas').press('Escape', {delay:80});
  await click(page, 600, 400);
  await click(page, 600, 515);
  await page.locator('#canvas').press('Home');
  for (let i=0;i<40;i++) await page.locator('#canvas').press('Delete');
  await page.locator('#canvas').pressSequentially('Browser regression', {delay:20});
  await click(page, 520, 555);
  const digest = () => page.evaluate(() => glob2Diagnostics.saveDigest('Browser_regression.game'));
  await expect.poll(digest).not.toBeNull();
  await expect.poll(async () => (await state(page)).persisting).toBe(false);
  const saved = await digest();
  expect(saved.size).toBeGreaterThan(1000);
  await page.reload();
  await screen(page, 'MainMenuScreen');
  expect(await digest()).toEqual(saved);
  // Each test owns a fresh browser context; only this test's save exists.
  expect(await page.evaluate(() => glob2Diagnostics.saves())).toEqual(['Browser_regression.game']);
  await clickMainMenu(page, 'load'); await screen(page, 'ChooseMapScreen');
  await menu(page, 100, 70);
  const downloadEvent = page.waitForEvent('download');
  await menu(page, 340, 320);
  const download = await downloadEvent;
  expect(download.suggestedFilename()).toBe('Browser_regression.game');
  const bytes = await require('node:fs/promises').readFile(await download.path());
  expect({size:bytes.length,sha256:require('node:crypto').createHash('sha256').update(bytes).digest('hex')}).toEqual(saved);
  await menu(page, 530, 440); await screen(page, 'MainMenuScreen');

  await clickMainMenu(page, 'load');
  await screen(page, 'ChooseMapScreen');
  await menu(page, 100, 70);
  await menu(page, 530, 380);
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThanOrEqual(paused.tick);
  await expect.poll(async () => (await state(page)).audio).toBe('running');
  expect(errors).toEqual([]);
});

test('editor setup and campaign entry dialogs return to their retained parents', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 90);
  await screen(page, 'NewMapScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 210);
  await screen(page, 'CampaignEditor');
  await menu(page, 80, 380);
  await screen(page, 'ChooseMapScreen');
  await menu(page, 100, 70);
  await menu(page, 530, 380);
  await screen(page, 'CampaignMapEntryEditor');
  await menu(page, 350, 450);
  await screen(page, 'CampaignEditor');
  await menu(page, 100, 60);
  await menu(page, 240, 380);
  await screen(page, 'CampaignMapEntryEditor');
  await menu(page, 540, 450);
  await screen(page, 'CampaignEditor');
  await menu(page, 540, 450);
  await screen(page, 'EditorMainMenu');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'MainMenuScreen');
  expect(errors).toEqual([]);
});

test('map editor frames resume after cancelling quit and can discard a new map', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 90);
  await screen(page, 'NewMapScreen');
  await menu(page, 160, 440);
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await click(page, 600, 525);
  await screen(page, 'MessageScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await click(page, 600, 525);
  await screen(page, 'MessageScreen');
  await menu(page, 320, 360);
  await screen(page, 'EditorMainMenu');
  expect(errors).toEqual([]);
});


test('editor save cancellation keeps edits open and completed fertility saves the map', async ({page}) => {
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 90);
  await screen(page, 'NewMapScreen');
  await menu(page, 160, 440);
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await click(page, 600, 525);
  await screen(page, 'MessageScreen');
  await menu(page, 110, 360); // Save before quit.
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80}); // Cancel file selection.
  await page.locator('#canvas').press('Escape', {delay:80}); // Reopen editor menu.
  await click(page, 600, 525);
  await screen(page, 'MessageScreen'); // Unsaved edits are still present.
  await menu(page, 110, 360);
  await screen(page, 'MapEditorScreen');
  await click(page, 600, 515);
  await page.locator('#canvas').press('Home');
  for (let i=0; i<40; ++i) await page.locator('#canvas').press('Delete');
  await page.locator('#canvas').pressSequentially('Browser editor', {delay:20});
  await click(page, 520, 555);
  await screen(page, 'EditorMainMenu'); // Returns only after job and map write complete.
  const digest = () => page.evaluate(() => glob2Diagnostics.mapDigest('Browser_editor.map'));
  await expect.poll(digest).not.toBeNull();
  await expect.poll(async () => (await state(page)).persisting).toBe(false);
  const saved = await digest();
  expect(saved.size).toBeGreaterThan(1000);
  await page.reload();
  await screen(page, 'MainMenuScreen');
  expect(await digest()).toEqual(saved);
});


test('editor map loading can be cancelled and restarted', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  for (const cancel of [true, false]) {
    await menu(page, 320, 150);
    await screen(page, 'ChooseMapScreen');
    await menu(page, 100, 70);
    if (cancel) await holdLoader(page, 'EditorLoadScreen');
    await menu(page, 530, 380);
    if (cancel) {
      await cancelHeldLoader(page, 'EditorLoadScreen');
    } else {
      await screen(page, 'MapEditorScreen');
      await page.locator('#canvas').press('Escape', {delay:80});
      await click(page, 600, 525);
    }
    await screen(page, 'EditorMainMenu');
  }
  expect(errors).toEqual([]);
});

test('custom and tutorial startup can be cancelled and retried', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
  await holdLoader(page, 'GameLoadScreen');
  await clickCustomGameStart(page);
  await cancelHeldLoader(page, 'GameLoadScreen');
  await screen(page, 'CustomGameScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'MainMenuScreen');
  await clickMainMenu(page, 'tutorial');
  await screen(page, 'CampaignMenuScreen');
  await menu(page, 100, 60);
  await holdLoader(page, 'GameLoadScreen');
  await menu(page, 160, 450);
  await cancelHeldLoader(page, 'GameLoadScreen');
  await screen(page, 'CampaignMenuScreen');
  await menu(page, 160, 450); // The same selected mission remains available.
  await screen(page, 'match');
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  expect(errors).toEqual([]);
});

test('a generated custom map loads after its setup screen closes', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  // Each generated preview lives in its own private temporary directory.
  const previews = () => page.evaluate(() => FS.readdir('/tmp').filter(name => name.startsWith('glob2-custom-')));
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
  const {width} = page.viewportSize();
  await click(page, Math.floor((width - Math.min(width - 32, 1120)) / 2) + 225, 100); // "Random map"
  await expect.poll(previews).toHaveLength(1);
  await clickCustomGameStart(page);
  await screen(page, 'match');
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  expect(await previews()).toEqual([]); // Removed once loading no longer needs it.
  expect(errors).toEqual([]);
});

test('cancelling an editor replacement preserves edits and a completed load replaces the map', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 90);
  await screen(page, 'NewMapScreen');
  await menu(page, 160, 440);
  await screen(page, 'MapEditorScreen');
  for (const cancel of [true, false]) {
    await page.locator('#canvas').press('Escape', {delay:80});
    await click(page, 600, 325); // Load map from inside the editor.
    await click(page, 500, 365);
    await holdLoader(page, 'EditorLoadScreen');
    await click(page, 520, 555);
    if (cancel) await cancelHeldLoader(page, 'EditorLoadScreen');
    else {
      await screen(page, 'EditorLoadScreen');
      await page.evaluate(() => releaseLoaderTurn());
    }
    await screen(page, 'MapEditorScreen');
    await page.locator('#canvas').press('Escape', {delay:80});
    await click(page, 600, 525);
    if (cancel) {
      await screen(page, 'MessageScreen'); // The original unsaved map is retained.
      await page.locator('#canvas').press('Escape', {delay:80});
      await screen(page, 'MapEditorScreen');
    } else await screen(page, 'EditorMainMenu'); // Replacement is unmodified.
  }
  expect(errors).toEqual([]);
});


for (const terrain of [{name:'swamp', y:140}, {name:'concrete islands', y:235}]) {
test(`map generation can be cancelled before retrying (${terrain.name})`, async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 90);
  await screen(page, 'NewMapScreen');
  await menu(page, 110, 60); // 256 columns.
  await menu(page, 110, 85); // 256 rows.
  await menu(page, 100, 235); // Concrete islands keep substantial work pending.
  await menu(page, 160, 440);
  await screen(page, 'EditorGenerateScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'NewMapScreen');
  await menu(page, 100, terrain.y); // Exercise height-map and partition jobs.
  await menu(page, 160, 440);
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await click(page, 600, 525);
  await screen(page, 'MessageScreen');
  await menu(page, 320, 360);
  await screen(page, 'EditorMainMenu');
  expect(errors).toEqual([]);
});

}
