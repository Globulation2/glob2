const {clickCreateMap}=require('./editor-controls');
const {gameURL,clickMainMenu,clickSettingsDone,clickCustomGameStart,clickControl,control}=require('./main-menu');
const {test, expect} = require('@playwright/test');
const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await snapshot(page)).screen).toContain(name);
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
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
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
  await clickControl(page,'quit'); await screen(page,'EndGameScreen');
  await page.locator('#canvas').press('Enter'); await screen(page,'CustomGameScreen');
});
test('editor dialogs and discard controls follow viewport changes', async ({page}) => {
  await clickMainMenu(page,'editor'); await screen(page,'EditorMainMenu');
  await clickControl(page,'new-map'); await screen(page,'NewMapScreen');
  await clickCreateMap(page); await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await resize(page,1280,720);
  // Unsaved work is decided on a card over the map, not on a separate page.
  await clickControl(page,'quit'); await control(page,'choice/1');
  expect((await snapshot(page)).screen).toContain('MapEditorScreen');
  await resize(page,1000,800);
  await clickControl(page,'choice/1'); await screen(page,'EditorMainMenu');
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

test.describe('initial wide viewport', () => {
  // Not 4:3, so the 800x600 placeholder's CSS fit must not decide the window size.
  test.use({viewport:{width:1280,height:554}});
  test('fills the page before any resize', async ({page}) => {
    expect(await snapshot(page)).toMatchObject({width:1280,height:554});
    expect(await page.locator('#canvas').boundingBox()).toMatchObject({x:0,y:0,width:1280,height:554});
    await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
  });
});

test.describe('high density display', () => {
  test.use({deviceScaleFactor:2});
  test('uses the renderer-appropriate backing resolution', async ({page}) => {
    // Firefox loses emulated density when COOP navigation replaces its process.
    // Reapply the real viewport so this still exercises a genuine 2x display.
    await page.setViewportSize(page.viewportSize());
    expect(await page.evaluate(() => devicePixelRatio)).toBe(2);
    // SDL3 high-density windows size the backing canvas for both software and
    // WebGL. Control bounds still map to CSS pixels once at the input boundary.
    const backingScale = 2;
    await resize(page,1100,750,backingScale);
    await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  });
});

test('interface scale survives resizing and keeps settings controls clickable', async ({page}, info) => {
  // Software applies scale immediately; WebGL follows master's restart requirement.
  test.skip((await snapshot(page)).renderer !== 'software');
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  await clickControl(page,'display.uiscale');
  await clickControl(page,'popup/1'); // Interface scale: 125%.
  await page.setViewportSize({width:1400,height:1000});
  await expect.poll(async () => (await snapshot(page)).width).toBe(1400);
  await page.screenshot({path:info.outputPath('scaled-settings-after-resize.png')});
  await clickSettingsDone(page); // Published bounds already account for the interface scale.
  await screen(page,'MainMenuScreen');
});
