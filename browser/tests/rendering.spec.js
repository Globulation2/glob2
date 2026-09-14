const {test, expect} = require('@playwright/test');
const {clickMainMenu,clickSettingsDone,clickCustomGameStart}=require('./main-menu');
const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const click = (page, x, y) => page.locator('#canvas').click({position:{x,y}, delay:80});

test('WebGL2 draws a playable match and resizes its drawing buffer', async ({page}, info) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  page.on('console', message => { if (/GL_INVALID|GL_INVALID_OPERATION|WebGL:.*(INVALID|error)|Aborted/.test(message.text())) errors.push(message.text()); });
  await page.goto('/?renderer=webgl2');
  await screen(page, 'MainMenuScreen');
  expect((await state(page)).renderer).toBe('webgl2');
  expect(await page.evaluate(() => document.querySelector('#canvas').getContext('webgl2') instanceof WebGL2RenderingContext)).toBe(true);
  await clickMainMenu(page,'custom'); await screen(page, 'CustomGameScreen');
  await clickCustomGameStart(page); // A fresh profile has a valid premade map preselected.
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.setViewportSize({width:1280,height:720});
  await expect.poll(async () => { const s = await state(page); return [s.width,s.height]; }).toEqual([1280,720]);
  expect(await page.evaluate(() => { const gl=document.querySelector('#canvas').getContext('webgl2'); return [gl.drawingBufferWidth,gl.drawingBufferHeight,gl.getError()]; })).toEqual([1280,720,0]);
  await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
  await page.screenshot({path:info.outputPath('webgl2-match.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,640,410); await screen(page,'EndGameScreen');
  expect(errors).toEqual([]);
});

test('software renderer remains available', async ({page}) => {
  await page.goto('/?renderer=software');
  await screen(page,'MainMenuScreen');
  expect((await state(page)).renderer).toBe('software');
  expect(await page.evaluate(() => Boolean(document.querySelector('#canvas').getContext('2d')))).toBe(true);
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
});

// Test browsers usually emulate WebGL2. These tests answer the shell's acceleration
// probe at the browser boundary; its renderer selection runs unchanged.
const UNMASKED_RENDERER_WEBGL = 0x9246;
test('the default renderer is WebGL2 when the browser accelerates it', async ({page}) => {
  await page.addInitScript(name => {
    const getContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, attributes) {
      return getContext.call(this, type, type === 'webgl2' ? {...attributes, failIfMajorPerformanceCaveat:false} : attributes);
    };
    const getParameter = WebGL2RenderingContext.prototype.getParameter;
    WebGL2RenderingContext.prototype.getParameter = function(parameter) {
      return parameter === name ? 'Test hardware GPU' : getParameter.call(this, parameter);
    };
  }, UNMASKED_RENDERER_WEBGL);
  await page.goto('/'); await screen(page,'MainMenuScreen');
  expect((await state(page)).renderer).toBe('webgl2');
  expect(await page.evaluate(() => document.querySelector('#canvas').getContext('webgl2') instanceof WebGL2RenderingContext)).toBe(true);
});

const fallbacks = {
  unavailable: () => {
    const getContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, attributes) {
      return type === 'webgl2' ? null : getContext.call(this, type, attributes);
    };
  },
  emulated: name => {
    const getParameter = WebGL2RenderingContext.prototype.getParameter;
    WebGL2RenderingContext.prototype.getParameter = function(parameter) {
      return parameter === name ? 'ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (LLVM 10.0.0)), SwiftShader driver)'
        : getParameter.call(this, parameter);
    };
  },
};
for (const [reason, stub] of Object.entries(fallbacks)) {
  test(`the default renderer falls back to software when WebGL2 is ${reason}`, async ({page}) => {
    await page.addInitScript(stub, UNMASKED_RENDERER_WEBGL);
    await page.goto('/'); await screen(page,'MainMenuScreen');
    expect((await state(page)).renderer).toBe('software');
    expect(await page.evaluate(() => Boolean(document.querySelector('#canvas').getContext('2d')))).toBe(true);
    await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  });
}


test('WebGL context restoration keeps the match and can recover repeatedly', async ({page}, info) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.goto('/?renderer=webgl2'); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page); // A fresh profile has a valid premade map preselected.
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  for (let count=1; count<=2; ++count) {
    await page.evaluate(() => {
      window.contextLoss = document.querySelector('#canvas').getContext('webgl2').getExtension('WEBGL_lose_context');
      contextLoss.loseContext();
    });
    await expect.poll(async () => (await state(page)).contextLost).toBe(true);
    const paused = (await state(page)).tick;
    // Exercise a real suspension interval, not just immediate restoration.
    await page.waitForTimeout(250);
    expect((await state(page)).tick).toBe(paused);
    await page.evaluate(() => contextLoss.restoreContext());
    await expect.poll(async () => (await state(page)).contextRestores).toBe(count);
    await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(paused);
    await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
    expect(await page.evaluate(() => document.querySelector('#canvas').getContext('webgl2').getError())).toBe(0);
  }
  await page.screenshot({path:info.outputPath('webgl2-restored.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,600,500); await screen(page,'EndGameScreen');
  expect(errors).toEqual([]);
});

test('WebGL context restoration retains settings, editor and confirmation controls', async ({page}, info) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.goto('/?renderer=webgl2');
  await screen(page, 'MainMenuScreen');
  let restores = 0;
  async function recover(expectedScreen) {
    await screen(page, expectedScreen);
    await page.evaluate(() => {
      window.contextLoss = document.querySelector('#canvas').getContext('webgl2').getExtension('WEBGL_lose_context');
      contextLoss.loseContext();
    });
    await expect.poll(async () => (await state(page)).contextLost).toBe(true);
    await page.evaluate(() => contextLoss.restoreContext());
    await expect.poll(async () => (await state(page)).contextRestores).toBe(++restores);
    await screen(page, expectedScreen);
    await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
    expect(await page.evaluate(() => document.querySelector('#canvas').getContext('webgl2').getError())).toBe(0);
  }
  await clickMainMenu(page,'settings'); await recover('SettingsScreen');
  await clickSettingsDone(page); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'editor'); await screen(page,'EditorMainMenu');
  await click(page,600,300); await screen(page,'NewMapScreen');
  await click(page,440,650); await recover('MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,600,525); await recover('MessageScreen');
  await page.screenshot({path:info.outputPath('webgl2-restored-editor-dialog.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,600,525); await screen(page,'MessageScreen');
  await click(page,600,570); await screen(page,'EditorMainMenu');
  expect(errors).toEqual([]);
});
