const {clickCreateMap,clickCampaignFooter,chooseEditorLandscape}=require('./editor-controls');
const {editTextField}=require('./main-menu');
const {gameURL,clickMainMenu,clickSettingsDone,clickCustomGameStart,clickCustomAIProfile,clickControl,clickListRow,controlBox}=require('./main-menu');
const {darkShare}=require('./pixels');
const {test, expect} = require('@playwright/test');

const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);

// Hold the first scheduled turn after entering a loader. The real Escape event
// can then reach a pending job without racing a fast machine's completed load.
// This holds the host frame boundary in either runtime without touching gameplay.
async function holdLoader(page, name) {
  await page.evaluate(name => {
    Module.glob2FrameGate = () => {
      if (!glob2Diagnostics.snapshot().screen.includes(name)) return true;
      window.releaseLoaderTurn = () => {
        delete window.releaseLoaderTurn;
        delete Module.glob2FrameGate;
      };
      return false;
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
  // Start from a known epoch, but let Date.now advance: SDL's browser clock
  // uses it for cooperative preview deadlines as well as generation seeds.
  if (info.title.startsWith('map generation can')) await page.clock.setSystemTime(12345 * 1000);
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
    await clickControl(page, 'new');
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
    await clickListRow(page, 'missions', 0);
    await clickControl(page, 'start');
    await screen(page, 'match');
    await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
    await page.locator('#canvas').press('Escape');
    await clickControl(page, 'quit');
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
  await clickControl(page, 'tab/2'); // Game Rules tab.
  await clickControl(page, 'ruleset/1'); // "Quick clash" tile.
  // The AI profile picker (Players & Teams tab, a colony's Info button) is a
  // screen CustomGameScreen pushes - see CustomGameScreen::showAIProfile.
  // It must actually be pushed, not blocking-executed: the browser host has
  // no Asyncify, so the old choose()/Screen::execute() pattern this replaced
  // threw and froze the page (docs/browser/adr-003-screen-execution.md).
  await clickControl(page, 'tab/1'); // Players & Teams tab.
  await clickCustomAIProfile(page, 3);
  await screen(page, 'CustomGameChoiceScreen');
  await clickListRow(page, 'profile/list', /Warrush/); // Warrush row.
  await clickControl(page, 'profile/use'); // "Use Warrush".
  await screen(page, 'CustomGameScreen');
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('Escape');
  await clickControl(page, 'quit');
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
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('p', {delay:80});
  await expect.poll(async () => (await state(page)).paused).toBe(true);
  const paused = await state(page);
  await expect.poll(async () => (await state(page)).frames).toBeGreaterThan(paused.frames + 10);
  expect((await state(page)).tick).toBe(paused.tick);

  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'save');
  await editTextField(page,'Browser regression');
  await clickControl(page, 'ok');
  const digest = () => page.evaluate(() => glob2Diagnostics.saveDigest('Browser_regression.game.gz'));
  await expect.poll(digest).not.toBeNull();
  await expect.poll(async () => (await state(page)).persisting).toBe(false);
  const saved = await digest();
  expect(saved.size).toBeGreaterThan(1000);
  await page.reload();
  await screen(page, 'MainMenuScreen');
  expect(await digest()).toEqual(saved);
  // Each test owns a fresh browser context; only this test's save exists.
  expect(await page.evaluate(() => glob2Diagnostics.saves())).toEqual(['Browser_regression.game.gz']);
  await clickMainMenu(page, 'load'); await screen(page, 'ChooseMapScreen');
  await clickListRow(page, 'files', 0);
  const downloadEvent = page.waitForEvent('download');
  await clickControl(page, 'export');
  const download = await downloadEvent;
  expect(download.suggestedFilename()).toBe('Browser_regression.game.gz');
  const bytes = await require('node:fs/promises').readFile(await download.path());
  expect({size:bytes.length,sha256:require('node:crypto').createHash('sha256').update(bytes).digest('hex')}).toEqual(saved);
  await clickControl(page, 'cancel'); await screen(page, 'MainMenuScreen');

  await clickMainMenu(page, 'load');
  await screen(page, 'ChooseMapScreen');
  await clickListRow(page, 'files', 0);
  await clickControl(page, 'ok');
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThanOrEqual(paused.tick);
  // Muted startup now leaves the audio device closed until the player unmutes.
  expect((await state(page)).audio).toBe('inactive');
  expect(errors).toEqual([]);
});

test('editor setup and campaign entry dialogs return to their retained parents', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await clickControl(page, 'new-map');
  await screen(page, 'NewMapScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'EditorMainMenu');
  await clickControl(page, 'new-campaign');
  await screen(page, 'CampaignEditor');
  await clickControl(page, 'add');
  await screen(page, 'ChooseMapScreen');
  await clickListRow(page, 'files', 0);
  await clickControl(page, 'ok');
  await screen(page, 'CampaignMapEntryEditor');
  await clickCampaignFooter(page,true);
  await screen(page, 'CampaignEditor');
  await clickListRow(page, 'maps', 0);
  await clickControl(page, 'edit');
  await screen(page, 'CampaignMapEntryEditor');
  await clickCampaignFooter(page,false);
  await screen(page, 'CampaignEditor');
  await clickCampaignFooter(page,false);
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
  await clickControl(page, 'new-map');
  await screen(page, 'NewMapScreen');
  await clickCreateMap(page);
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'quit');
  await screen(page, 'MessageScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'quit');
  await screen(page, 'MessageScreen');
  await clickControl(page, 'choice/1'); // Quit without saving.
  await screen(page, 'EditorMainMenu');
  expect(errors).toEqual([]);
});


test('editor save cancellation keeps edits open and completed fertility saves the map', async ({page}) => {
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await clickControl(page, 'new-map');
  await screen(page, 'NewMapScreen');
  await clickCreateMap(page);
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'quit');
  await screen(page, 'MessageScreen');
  await clickControl(page, 'choice/0'); // Save before quit.
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80}); // Cancel file selection.
  await page.locator('#canvas').press('Escape', {delay:80}); // Reopen editor menu.
  await clickControl(page, 'quit');
  await screen(page, 'MessageScreen'); // Unsaved edits are still present.
  await clickControl(page, 'choice/0');
  await screen(page, 'MapEditorScreen');
  await editTextField(page,'Browser editor');
  await clickControl(page, 'ok');
  await screen(page, 'EditorMainMenu'); // Returns only after job and map write complete.
  const digest = () => page.evaluate(() => glob2Diagnostics.mapDigest('Browser_editor.map.gz'));
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
    await clickControl(page, 'load-map');
    await screen(page, 'ChooseMapScreen');
    await clickListRow(page, 'files', 0);
    if (cancel) await holdLoader(page, 'EditorLoadScreen');
    await clickControl(page, 'ok');
    if (cancel) {
      await cancelHeldLoader(page, 'EditorLoadScreen');
    } else {
      await screen(page, 'MapEditorScreen');
      await page.locator('#canvas').press('Escape', {delay:80});
      await clickControl(page, 'quit');
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
  await clickListRow(page, 'missions', 0);
  await holdLoader(page, 'GameLoadScreen');
  await clickControl(page, 'start');
  await cancelHeldLoader(page, 'GameLoadScreen');
  await screen(page, 'CampaignMenuScreen');
  await clickControl(page, 'start'); // The same selected mission remains available.
  await screen(page, 'match');
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  expect(errors).toEqual([]);
});

test('a generated custom map loads after its setup screen closes', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
  await clickControl(page, 'map/mode/1'); // "Random map"
  await clickCustomGameStart(page);
  await screen(page, 'match');
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  expect(errors).toEqual([]);
});

test('the custom game preview draws the selected map', async ({page}) => {
  await clickMainMenu(page, 'custom');
  await screen(page, 'CustomGameScreen');
  // The preview square on the Map tab, as the lobby lays it out.
  const clip = await controlBox(page, 'map/preview');
  // A fresh profile previews FourSquares1: water and grass, not a flat panel.
  await expect.poll(() => darkShare(page, clip)).toBeGreaterThan(0.25);
});

test('cancelling an editor replacement preserves edits and a completed load replaces the map', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await clickControl(page, 'new-map');
  await screen(page, 'NewMapScreen');
  await clickCreateMap(page);
  await screen(page, 'MapEditorScreen');
  for (const cancel of [true, false]) {
    await page.locator('#canvas').press('Escape', {delay:80});
    await clickControl(page, 'load'); // Load map from inside the editor.
    await clickListRow(page, 'files', 0);
    await holdLoader(page, 'EditorLoadScreen');
    await clickControl(page, 'ok');
    if (cancel) await cancelHeldLoader(page, 'EditorLoadScreen');
    else {
      await screen(page, 'EditorLoadScreen');
      await page.evaluate(() => releaseLoaderTurn());
    }
    await screen(page, 'MapEditorScreen');
    await page.locator('#canvas').press('Escape', {delay:80});
    await clickControl(page, 'quit');
    if (cancel) {
      await screen(page, 'MessageScreen'); // The original unsaved map is retained.
      await page.locator('#canvas').press('Escape', {delay:80});
      await screen(page, 'MapEditorScreen');
    } else await screen(page, 'EditorMainMenu'); // Replacement is unmodified.
  }
  expect(errors).toEqual([]);
});


for (const terrain of [{name:'swamp'}, {name:'concrete islands'}]) {
test(`map generation can be cancelled before retrying (${terrain.name})`, async ({page},info) => {
  // Two visits generate the visible previews cooperatively on the UI thread.
  test.setTimeout(180000);
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await clickMainMenu(page, 'editor');
  await screen(page, 'EditorMainMenu');
  await clickControl(page, 'new-map');
  await screen(page, 'NewMapScreen');
  // The preview starts at 256 × 256; choose a partition generator first.
  await chooseEditorLandscape(page,'concrete islands');
  await holdLoader(page, 'EditorGenerateScreen');
  await clickCreateMap(page);
  await cancelHeldLoader(page, 'EditorGenerateScreen');
  await screen(page, 'NewMapScreen');
  await chooseEditorLandscape(page,terrain.name); // Exercise height-map and partition jobs.
  await page.screenshot({path:info.outputPath('selected-editor-landscape.png')});
  await clickCreateMap(page);
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'quit');
  await screen(page, 'MessageScreen');
  await clickControl(page, 'choice/1');
  await screen(page, 'EditorMainMenu');
  expect(errors).toEqual([]);
});

}

test('landscape picker and start-quality details return to the retained custom setup', async ({page}, info) => {
  await page.goto(gameURL()); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickControl(page,'map/mode/1'); // Random map.
  await clickControl(page,'generator/landscape'); await screen(page,'LandscapePickerScreen');
  await page.screenshot({path:info.outputPath('landscape-picker.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'CustomGameScreen');
  // The info button appears once the preview has finished generating.
  await clickControl(page,'quality/info',{timeout:60000});
  await screen(page,'StartQualityScreen');
  await page.screenshot({path:info.outputPath('start-quality.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page);
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
});
