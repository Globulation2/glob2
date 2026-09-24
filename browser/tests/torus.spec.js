const {gameURL, clickMainMenu, clickCustomGameStart} = require('./main-menu');
const {test, expect} = require('@playwright/test');
const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const toggle = page => page.locator('#canvas').press('g', {delay: 80});
// Bottom-left of the play area at the default 1200x900 viewport: open sky below
// the fitted ring in the overview, fog or ground on the flat map.
const corner = {x: 20, y: 760, width: 200, height: 100};
// The 1.8 s transition advances at most 0.1 s per frame. SwiftShader draws the
// overview at about 3 fps, where the transition takes several seconds.
const transition = {timeout: 45000};

function urlWith(renderer) {
  const url = new URL(gameURL(), 'http://localhost');
  url.searchParams.set('renderer', renderer);
  return url.pathname + url.search;
}

async function startMatch(page, renderer) {
  await page.goto(urlWith(renderer)); await screen(page, 'MainMenuScreen');
  await clickMainMenu(page, 'custom'); await screen(page, 'CustomGameScreen');
  await clickCustomGameStart(page); // A fresh profile has a valid premade map preselected.
  await expect.poll(async () => (await state(page)).tick, {timeout: 60000}).toBeGreaterThan(25);
}

// Share of a clip matching the overview's sky clear colour, rgb(6, 9, 15). The
// flat map clears to rgb(0, 0, 32) and fog is black, so neither matches.
async function skyShare(page, clip) {
  const png = await page.screenshot({clip});
  return page.evaluate(async base64 => {
    const blob = await (await fetch('data:image/png;base64,' + base64)).blob();
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = bitmap.width; canvas.height = bitmap.height;
    const context = canvas.getContext('2d'); context.drawImage(bitmap, 0, 0); bitmap.close();
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let sky = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (Math.abs(pixels[i] - 6) <= 5 && Math.abs(pixels[i+1] - 9) <= 5 && Math.abs(pixels[i+2] - 15) <= 5) ++sky;
    return sky / (pixels.length / 4);
  }, png.toString('base64'));
}

const glError = page => page.evaluate(() => document.querySelector('#canvas').getContext('webgl2').getError());

test('WebGL2 switches between the flat map and the torus overview', async ({page}, info) => {
  test.setTimeout(180000);
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await startMatch(page, 'webgl2');
  expect(await state(page)).toMatchObject({renderer: 'webgl2', torus: false});
  expect(await skyShare(page, corner)).toBeLessThan(0.5);

  await toggle(page);
  await expect.poll(async () => (await state(page)).torus).toBe(true);
  await expect.poll(() => skyShare(page, corner), transition).toBeGreaterThan(0.6);
  await page.screenshot({path: info.outputPath('torus-overview.png')});
  expect(await glError(page)).toBe(0);
  const tick = (await state(page)).tick;
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(tick);

  await toggle(page);
  await expect.poll(async () => (await state(page)).torus, transition).toBe(false);
  await expect.poll(() => skyShare(page, corner)).toBeLessThan(0.5);
  expect(await glError(page)).toBe(0);
  expect(errors).toEqual([]);
});

test('the torus overview recovers after WebGL context loss', async ({page}) => {
  test.setTimeout(180000);
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await startMatch(page, 'webgl2');
  await toggle(page);
  await expect.poll(() => skyShare(page, corner), transition).toBeGreaterThan(0.6);

  await page.evaluate(() => {
    window.contextLoss = document.querySelector('#canvas').getContext('webgl2').getExtension('WEBGL_lose_context');
    contextLoss.loseContext();
  });
  await expect.poll(async () => (await state(page)).contextLost).toBe(true);
  await page.evaluate(() => contextLoss.restoreContext());
  await expect.poll(async () => (await state(page)).contextRestores).toBe(1);
  await expect.poll(async () => (await state(page)).torus, transition).toBe(true);
  await expect.poll(() => skyShare(page, corner), transition).toBeGreaterThan(0.6);
  expect(await glError(page)).toBe(0);
  expect(errors).toEqual([]);
});

test('the software renderer keeps the flat map', async ({page}) => {
  test.setTimeout(120000);
  await startMatch(page, 'software');
  await toggle(page);
  await page.waitForTimeout(1500);
  expect((await state(page)).torus).toBe(false);
  expect(await skyShare(page, corner)).toBeLessThan(0.5);
});
