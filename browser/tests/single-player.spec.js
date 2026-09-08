const {test, expect} = require('@playwright/test');

const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const click = (page, x, y) => page.locator('#canvas').click({position:{x,y}, delay:80});
const menu = (page, x, y) => click(page, x + 280, y + 210);

test.beforeEach(async ({page}) => {
  await page.goto('/');
  await screen(page, 'MainMenuScreen');
});

test('starts with a full-page game and restored local storage', async ({page}) => {
  expect(await state(page)).toMatchObject({width:1200, height:900, restore:'ready'});
  await expect(page.locator('button')).toHaveCount(0);
  expect(await page.locator('body').innerText()).toBe('');
  expect(await page.locator('#canvas').boundingBox()).toMatchObject({x:0,y:0,width:1200,height:900});
});

test('application host returns from settings and credits and shuts down cleanly', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await menu(page, 160, 360);
  await screen(page, 'SettingsScreen');
  await menu(page, 530, 440);
  await screen(page, 'MainMenuScreen');
  await menu(page, 160, 440);
  await screen(page, 'CreditScreen');
  await page.locator('#canvas').press('Escape');
  await screen(page, 'MainMenuScreen');
  await menu(page, 480, 440);
  await screen(page, 'exited');
  expect(errors).toEqual([]);
});

test('campaign selector returns to its suspended parent and can reopen', async ({page}) => {
  await menu(page, 160, 120);
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
  await menu(page, 480, 120);
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

test('custom options and AI descriptions return to setup, and a finished game returns there too', async ({page}) => {
  await menu(page, 480, 200);
  await screen(page, 'CustomGameScreen');
  await menu(page, 100, 70);
  await menu(page, 310, 440);
  await screen(page, 'CustomGameOtherOptions');
  await page.locator('#canvas').press('Escape');
  await screen(page, 'CustomGameScreen');
  await menu(page, 310, 390);
  await screen(page, 'AIDescriptionScreen');
  await page.locator('#canvas').press('Enter');
  await screen(page, 'CustomGameScreen');
  await menu(page, 530, 380);
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('Escape');
  await click(page, 600, 500);
  await screen(page, 'EndGameScreen');
  await page.locator('#canvas').press('Enter');
  await screen(page, 'CustomGameScreen');
  await page.locator('#canvas').press('Escape');
  await screen(page, 'MainMenuScreen');
});

test('custom match pauses, persists and resumes after reload', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await menu(page, 480, 200);
  await screen(page, 'CustomGameScreen');
  await menu(page, 100, 70);
  await menu(page, 530, 380);
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
  await menu(page, 160, 200);
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
  await menu(page, 480, 360);
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
  await menu(page, 480, 360);
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
  await menu(page, 480, 360);
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
  await menu(page, 480, 360);
  await screen(page, 'EditorMainMenu');
  for (const cancel of [true, false]) {
    await menu(page, 320, 150);
    await screen(page, 'ChooseMapScreen');
    await menu(page, 100, 70);
    await menu(page, 530, 380);
    if (cancel) {
      await screen(page, 'EditorLoadScreen');
      await page.locator('#canvas').press('Escape', {delay:80});
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
  await menu(page, 480, 200);
  await screen(page, 'CustomGameScreen');
  await menu(page, 100, 70);
  await menu(page, 530, 380);
  await screen(page, 'GameLoadScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'CustomGameScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'MainMenuScreen');
  await menu(page, 480, 120);
  await screen(page, 'CampaignMenuScreen');
  await menu(page, 100, 60);
  await menu(page, 160, 450);
  await screen(page, 'GameLoadScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'CampaignMenuScreen');
  await menu(page, 160, 450); // The same selected mission remains available.
  await screen(page, 'match');
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  expect(errors).toEqual([]);
});

test('cancelling an editor replacement preserves edits and a completed load replaces the map', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await menu(page, 480, 360);
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 90);
  await screen(page, 'NewMapScreen');
  await menu(page, 160, 440);
  await screen(page, 'MapEditorScreen');
  for (const cancel of [true, false]) {
    await page.locator('#canvas').press('Escape', {delay:80});
    await click(page, 600, 325); // Load map from inside the editor.
    await click(page, 500, 365);
    await click(page, 520, 555);
    await screen(page, 'EditorLoadScreen');
    if (cancel) await page.locator('#canvas').press('Escape', {delay:80});
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


test('map generation can be cancelled before retrying', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await menu(page, 480, 360);
  await screen(page, 'EditorMainMenu');
  await menu(page, 320, 90);
  await screen(page, 'NewMapScreen');
  await menu(page, 110, 60); // 256 columns.
  await menu(page, 110, 85); // 256 rows.
  await menu(page, 160, 440);
  await screen(page, 'EditorGenerateScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page, 'NewMapScreen');
  await menu(page, 100, 140); // Swamp: exercises the resumable height-map passes.
  await menu(page, 160, 440);
  await screen(page, 'MapEditorScreen');
  await page.locator('#canvas').press('Escape', {delay:80});
  await click(page, 600, 525);
  await screen(page, 'MessageScreen');
  await menu(page, 320, 360);
  await screen(page, 'EditorMainMenu');
  expect(errors).toEqual([]);
});
