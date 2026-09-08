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

test('launches the first tutorial mission through campaign controls', async ({page}) => {
  await menu(page, 480, 120);
  await screen(page, 'CampaignMenuScreen');
  await menu(page, 100, 60);
  await menu(page, 160, 450);
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
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
