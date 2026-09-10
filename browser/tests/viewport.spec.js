const {gameURL,clickMainMenu,clickSettingsDone,clickCustomGameStart}=require('./main-menu');
const {test, expect} = require('@playwright/test');
const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await snapshot(page)).screen).toContain(name);
const click = (page,x,y) => page.locator('#canvas').click({position:{x,y},delay:80});
const menu = (page,x,y) => { const {width,height}=page.viewportSize(); return click(page,x+(width-640)/2,y+(height-480)/2); };
async function resize(page,width,height,backingScale=1) {
  await page.setViewportSize({width,height});
  await expect.poll(async () => { const s=await snapshot(page); return [s.width,s.height]; }).toEqual([width*backingScale,height*backingScale]);
  expect(await page.locator('#canvas').boundingBox()).toMatchObject({x:0,y:0,width,height});
  // Input and dimensions can work even when an offscreen surface is never presented.
  await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
}
test.beforeEach(async ({page}) => { await page.goto(gameURL()); await screen(page,'MainMenuScreen'); });
test('menus follow the viewport and keep their controls clickable', async ({page}) => {
  await resize(page,1280,720);
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  await resize(page,900,650);
  await clickSettingsDone(page); await screen(page,'MainMenuScreen');
  await resize(page,1440,900);
  await clickMainMenu(page,'credits'); await screen(page,'CreditScreen');
  await page.locator('#canvas').press('Escape'); await screen(page,'MainMenuScreen');
});
test('a running match survives resize and its open menu follows the new center', async ({page}, info) => {
  // Cold texture creation on a headless software GPU can dominate startup.
  test.setTimeout(120000);
  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page); // A fresh profile has a valid premade map preselected.
  await expect.poll(async () => (await snapshot(page)).tick, {timeout:60000}).toBeGreaterThan(25);
  const before=(await snapshot(page)).tick;
  await resize(page,1400,800); await resize(page,900,650);
  await expect.poll(async () => (await snapshot(page)).tick).toBeGreaterThan(before);
  await resize(page,500,400);
  expect((await snapshot(page)).screenClass).toContain('GameSessionScreen');
  const small = (await snapshot(page)).tick;
  await expect.poll(async () => (await snapshot(page)).tick).toBeGreaterThan(small);
  await resize(page,900,650);
  const beforeMenu = (await snapshot(page)).frames;
  await page.locator('#canvas').press('Escape',{delay:80});
  await expect.poll(async () => (await snapshot(page)).frames).toBeGreaterThan(beforeMenu + 2);
  await resize(page,1280,720);
  await page.screenshot({path:info.outputPath('resized-game-menu.png')});
  await click(page,640,410); await screen(page,'EndGameScreen');
  await page.locator('#canvas').press('Enter'); await screen(page,'CustomGameScreen');
});
test('editor dialogs and discard controls follow viewport changes', async ({page}) => {
  await clickMainMenu(page,'editor'); await screen(page,'EditorMainMenu');
  await menu(page,320,90); await screen(page,'NewMapScreen');
  await menu(page,160,440); await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await resize(page,1280,720);
  await click(page,640,435); await screen(page,'MessageScreen');
  await resize(page,1000,800);
  await menu(page,320,360); await screen(page,'EditorMainMenu');
});

test('small viewports retain the active screen and continue rendering', async ({page}, info) => {
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  await resize(page,500,400); await screen(page,'SettingsScreen');
  await page.screenshot({path:info.outputPath('small-viewport.png')});
  await resize(page,1100,700); await screen(page,'SettingsScreen');
  await clickSettingsDone(page); await screen(page,'MainMenuScreen');
});

test.describe('initial small viewport', () => {
  test.use({viewport:{width:500,height:400}});
  test('starts at the actual browser dimensions', async ({page}) => {
    expect(await snapshot(page)).toMatchObject({width:500,height:400});
    await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
    await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  });
});

test.describe('high density display', () => {
  test.use({deviceScaleFactor:2});
  test('uses the renderer-appropriate backing resolution', async ({page}) => {
    expect(await page.evaluate(() => devicePixelRatio)).toBe(2);
    const backingScale = (await snapshot(page)).renderer === 'webgl2' ? 2 : 1;
    await resize(page,1100,750,backingScale);
    await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  });
});
