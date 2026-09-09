const {resizeAndWait}=require('./viewport-ready');
const {test, expect} = require('@playwright/test');
const {gameURL} = require('./game-url');
const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const click = (page, x, y) => page.locator('#canvas').click({position:{x,y}, delay:80});

test('corrupt local files leave the chooser responsive and cannot accept an old selection', async ({page}) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.goto(gameURL()); await screen(page, 'MainMenuScreen');
  // Inject damaged local storage, rather than bypassing the import validator.
  // The valid fixture is only selected for its header/preview, never played.
  await page.evaluate(() => {
    FS.writeFile('/home/web_user/.glob2/games/AAA_Valid.game', FS.readFile('/maps/balanced.map'));
    FS.writeFile('/home/web_user/.glob2/games/AAB_Corrupt.game', new Uint8Array([98,97,100]));
  });
  await click(page,440,410); await screen(page,'ChooseMapScreen');
  await click(page,380,280);
  await click(page,380,300);
  await page.locator('#canvas').press('Enter', {delay:80});
  await screen(page,'ChooseMapScreen');
  await resizeAndWait(page, {width:800,height:600});
  await expect.poll(async () => (await state(page)).width).toBe(800);
  await page.locator('#canvas').press('Escape', {delay:80});
  await screen(page,'MainMenuScreen');
  expect(errors).toEqual([]);
});
