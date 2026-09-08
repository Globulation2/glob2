const {test, expect} = require('@playwright/test');
const snapshot = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await snapshot(page)).screen).toContain(name);
const click = (page,x,y) => page.locator('#canvas').click({position:{x,y},delay:80});
const menu = (page,x,y) => { const {width,height}=page.viewportSize(); return click(page,x+(width-640)/2,y+(height-480)/2); };
async function resize(page,width,height) {
  await page.setViewportSize({width,height});
  await expect.poll(async () => { const s=await snapshot(page); return [s.width,s.height]; }).toEqual([width,height]);
  expect(await page.locator('#canvas').boundingBox()).toMatchObject({x:0,y:0,width,height});
  // Input and dimensions can work even when an offscreen surface is never presented.
  await expect.poll(() => page.evaluate(() => {
    const canvas = document.getElementById('canvas');
    const pixels = canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;
    return pixels.some((value,index) => index % 4 !== 3 && value !== 0);
  })).toBe(true);
}
test.beforeEach(async ({page}) => { await page.goto('/'); await screen(page,'MainMenuScreen'); });
test('menus follow the viewport and keep their controls clickable', async ({page}) => {
  await resize(page,1280,720);
  await menu(page,160,360); await screen(page,'SettingsScreen');
  await resize(page,900,650);
  await menu(page,530,440); await screen(page,'MainMenuScreen');
  await resize(page,1440,900);
  await menu(page,160,440); await screen(page,'CreditScreen');
  await page.locator('#canvas').press('Escape'); await screen(page,'MainMenuScreen');
});
test('a running match survives resize and its open menu follows the new center', async ({page}, info) => {
  await menu(page,480,200); await screen(page,'CustomGameScreen');
  await menu(page,100,70); await menu(page,530,380);
  await expect.poll(async () => (await snapshot(page)).tick).toBeGreaterThan(25);
  const before=(await snapshot(page)).tick;
  await resize(page,1400,800); await resize(page,900,650);
  await expect.poll(async () => (await snapshot(page)).tick).toBeGreaterThan(before);
  await resize(page,500,400); await screen(page,'MinimumViewportScreen');
  const suspended = (await snapshot(page)).tick;
  await page.locator('#canvas').press('Escape',{delay:80});
  expect((await snapshot(page)).tick).toBe(suspended);
  await resize(page,900,650);
  await expect.poll(async () => (await snapshot(page)).tick).toBeGreaterThan(suspended);
  const beforeMenu = (await snapshot(page)).frames;
  await page.locator('#canvas').press('Escape',{delay:80});
  await expect.poll(async () => (await snapshot(page)).frames).toBeGreaterThan(beforeMenu + 2);
  await resize(page,1280,720);
  await page.screenshot({path:info.outputPath('resized-game-menu.png')});
  await click(page,640,410); await screen(page,'EndGameScreen');
  await page.locator('#canvas').press('Enter'); await screen(page,'CustomGameScreen');
});
test('editor dialogs and discard controls follow viewport changes', async ({page}) => {
  await menu(page,480,360); await screen(page,'EditorMainMenu');
  await menu(page,320,90); await screen(page,'NewMapScreen');
  await menu(page,160,440); await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await resize(page,1280,720);
  await click(page,640,435); await screen(page,'MessageScreen');
  await resize(page,1000,800);
  await menu(page,320,360); await screen(page,'EditorMainMenu');
});

test('small viewports show an in-game notice and return to the retained menu', async ({page}, info) => {
  await menu(page,160,360); await screen(page,'SettingsScreen');
  await resize(page,500,400); await screen(page,'MinimumViewportScreen');
  await page.screenshot({path:info.outputPath('minimum-viewport.png')});
  await page.locator('#canvas').press('Escape'); await screen(page,'MinimumViewportScreen');
  await resize(page,1100,700); await screen(page,'SettingsScreen');
  await menu(page,530,440); await screen(page,'MainMenuScreen');
});

test.describe('high density display', () => {
  test.use({deviceScaleFactor:2});
  test('keeps one rendering pixel per CSS pixel', async ({page}) => {
    expect(await page.evaluate(() => devicePixelRatio)).toBe(2);
    await resize(page,1100,750);
    await menu(page,160,360); await screen(page,'SettingsScreen');
  });
});
