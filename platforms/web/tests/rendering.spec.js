const {waitForMainMenu}=require('./main-menu');
const {clickCreateMap}=require('./editor-controls');
const {test, expect} = require('@playwright/test');
const {clickMainMenu,clickSettingsDone,clickCustomGameStart,clickControl,control}=require('./main-menu');
const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => name==='MainMenuScreen' ? waitForMainMenu(page) : expect.poll(async () => (await state(page)).screen).toContain(name);

test('WebGL2 draws a playable match and resizes its drawing buffer', async ({page}, info) => {
  test.setTimeout(120000);
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  page.on('console', message => { if (/GL_INVALID|GL_INVALID_OPERATION|WebGL:.*(INVALID|error)|GPU mip preparation failed|Aborted/.test(message.text())) errors.push(message.text()); });
  await page.goto('/?renderer=webgl2&gl-errors=1');
  await screen(page, 'MainMenuScreen');
  expect((await state(page)).renderer).toBe('webgl2');
  expect((await state(page)).renderContext.width).toBeGreaterThan(0);
  await clickMainMenu(page,'custom'); await screen(page, 'CustomGameScreen');
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(25);
  await page.setViewportSize({width:1280,height:720});
  await expect.poll(async () => { const s = await state(page); return [s.width,s.height]; }).toEqual([1280,720]);
  await expect.poll(async () => { const gl=(await state(page)).renderContext; return [gl.width,gl.height,gl.error]; }).toEqual([1280,720,0]);
  await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
  await page.screenshot({path:info.outputPath('webgl2-match.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'quit'); await screen(page,'EndGameScreen');
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
for (const variant of ['serial','threaded']) {
test(`the default renderer is WebGL2 when the browser accelerates it (${variant})`, async ({page}) => {
  await page.addInitScript(name => {
    const getContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, attributes) {
      return getContext.call(this, type, type === 'webgl2' ? {...attributes, failIfMajorPerformanceCaveat:false} : attributes);
    };
    const getParameter = WebGL2RenderingContext.prototype.getParameter;
    WebGL2RenderingContext.prototype.getParameter = function(parameter) {
      return parameter === name || parameter === this.RENDERER ? 'Test hardware GPU' : getParameter.call(this, parameter);
    };
  }, UNMASKED_RENDERER_WEBGL);
  await page.goto('/?threads='+variant); await screen(page,'MainMenuScreen');
  const runtime = await state(page);
  test.skip(runtime.executionMode !== variant, `Interactive ${variant} runtime unavailable: ${runtime.threadFallback}`);
  expect(runtime.renderer).toBe('webgl2');
  expect((await state(page)).renderContext.width).toBeGreaterThan(0);
  expect((await state(page)).renderContext.error).toBeNull();
});

}

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


for (const variant of ['serial', 'threaded']) {
test(`WebGL context restoration keeps the match and can recover repeatedly (${variant})`, async ({page}, info) => {
  test.setTimeout(240000);
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.goto(`/?renderer=webgl2&gl-errors=1&threads=${variant}`); await screen(page,'MainMenuScreen');
  await expect.poll(async () => (await state(page)).assets.hd, {timeout:120000}).toBe('ready');
  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(25);
  await expect.poll(async () => (await state(page)).artworkReady, {timeout:120000}).toBe(true);
  for (let count=1; count<=2; ++count) {
    await page.evaluate(() => {
      Module._glob2_context_action(0);
    });
    await expect.poll(async () => (await state(page)).contextLost).toBe(true);
    const suspended = await state(page);
    const paused = suspended.tick;
    if (count === 1) await page.evaluate(() => {
      Object.defineProperty(document, 'hidden', {configurable:true, get:() => true});
      document.dispatchEvent(new Event('visibilitychange'));
    });
    const viewport = count === 1 ? {width:1280,height:720} : {width:1200,height:900};
    await page.setViewportSize(viewport);
    // Exercise a real suspension interval, not just immediate restoration.
    await page.waitForTimeout(250);
    expect((await state(page)).tick).toBe(paused);
    await page.evaluate(() => Module._glob2_context_action(1));
    await expect.poll(async () => (await state(page)).contextRestores, {timeout:60000}).toBe(count);
    if (count === 1) {
      // Restoring the GPU must not resume a tab that is still hidden.
      const restored = await state(page);
      await expect.poll(async () => (await state(page)).loop).toBeGreaterThan(restored.loop + 3);
      expect((await state(page)).tick).toBe(paused);
      expect((await state(page)).frames).toBe(suspended.frames);
      await page.evaluate(() => {
        delete document.hidden;
        document.dispatchEvent(new Event('visibilitychange'));
      });
    }
    await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(paused);
    await expect.poll(async () => {
      const s = await state(page);
      return [s.width,s.height,s.renderContext.width,s.renderContext.height];
    }).toEqual([viewport.width,viewport.height,viewport.width,viewport.height]);
    await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
    expect((await state(page)).renderContext.error).toBe(0);
  }
  await page.screenshot({path:info.outputPath('webgl2-restored.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'quit'); await screen(page,'EndGameScreen');
  expect(errors).toEqual([]);
});
}

test('WebGL context restoration retains settings, editor and confirmation controls', async ({page}, info) => {
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.goto('/?renderer=webgl2&gl-errors=1');
  await screen(page, 'MainMenuScreen');
  let restores = 0;
  async function recover(expectedScreen) {
    await screen(page, expectedScreen);
    await page.evaluate(() => {
      Module._glob2_context_action(0);
    });
    await expect.poll(async () => (await state(page)).contextLost).toBe(true);
    await page.evaluate(() => Module._glob2_context_action(1));
    await expect.poll(async () => (await state(page)).contextRestores).toBe(++restores);
    await screen(page, expectedScreen);
    await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
    expect((await state(page)).renderContext.error).toBe(0);
  }
  await clickMainMenu(page,'settings'); await recover('SettingsScreen');
  await clickSettingsDone(page); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'editor'); await screen(page,'EditorMainMenu');
  await clickControl(page,'new-map'); await screen(page,'NewMapScreen');
  await clickCreateMap(page); await recover('MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'quit'); await control(page,'choice/2'); await recover('MapEditorScreen');
  await page.screenshot({path:info.outputPath('webgl2-restored-editor-dialog.png')});
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'quit'); await clickControl(page,'choice/1'); await screen(page,'EditorMainMenu');
  expect(errors).toEqual([]);
});

for (const variant of ['serial', 'threaded']) {
  test(`HD terrain requested by the match can arrive late (${variant})`, async ({page}, info) => {
    test.setTimeout(240000);
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    let release;
    const held = new Promise(resolve => { release = resolve; });
    const familyState = async () => Object.entries((await state(page)).assets)
      .filter(([name]) => name.startsWith('hd-terrain-'));
    await page.route('**/assets/hd-terrain-*.data', async route => {
      await held;
      await route.continue();
    });
    // Observe terrain-sized responses from the real worker. Global artwork
    // readiness also waits on unit and building sprites and animated lookahead.
    await page.addInitScript(() => {
      window.decodedTerrainGroups = 0;
      const NativeWorker = Worker;
      window.Worker = class extends NativeWorker {
        constructor(url, options) {
          super(url, options);
          if (!/\/asset-(?:[a-f0-9]+-)?video-worker\.js$/.test(new URL(url, document.baseURI).pathname)) return;
          const terrain = new Set();
          const post = this.postMessage.bind(this);
          this.postMessage = (message, ...transfer) => {
            if (message.width > 128 || message.height > 128) terrain.add(message.id);
            return post(message, ...transfer);
          };
          this.addEventListener('message', ({data}) => {
            if (terrain.delete(data.id) && data.rgba) ++window.decodedTerrainGroups;
          });
        }
      };
    });
    await page.goto(`/?renderer=webgl2&gl-errors=1&threads=${variant}`);
    await screen(page, 'MainMenuScreen');
    expect((await familyState()).every(([, status]) => status === 'idle')).toBe(true);
    await clickMainMenu(page, 'custom');
    await screen(page, 'CustomGameScreen');
    try {
      const entered = Date.now();
      await clickCustomGameStart(page);
      await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(25);
      await expect.poll(async () => (await familyState()).some(([, status]) => status === 'downloading')).toBe(true);
      expect((await familyState()).some(([, status]) => status === 'ready')).toBe(false);
      await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
      await page.screenshot({path:info.outputPath('native-while-hd-is-held.png')});
      const resumed = Date.now();
      release();
      await expect.poll(async () => {
        const families = await familyState();
        return families.some(([, status]) => status === 'ready') &&
          families.every(([, status]) => status !== 'downloading');
      }, {timeout:120000}).toBe(true);
      const installed = Date.now();
      const videoFamilies = await page.evaluate(() => Module.glob2AssetManifest.packages.some(entry =>
        entry.name.startsWith('hd-terrain-') && Module.glob2Assets.states[entry.name] === 'ready' &&
        entry.parts.some(part => part.files.some(([path]) => path.endsWith('.h264')))));
      if (videoFamilies)
        await expect.poll(() => page.evaluate(() => window.decodedTerrainGroups), {timeout:120000}).toBeGreaterThan(0);
      await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
      await info.attach('terrain-delivery', {contentType:'application/json', body:JSON.stringify({
        variant, entryToPackagesMs:installed - entered, releaseToPackagesMs:installed - resumed,
        releaseToRenderedMs:Date.now() - resumed,
        families:Object.fromEntries(await familyState()),
      })});
      expect((await familyState()).some(([, status]) => status === 'idle')).toBe(true);
      await page.screenshot({path:info.outputPath('after-hd-arrival.png')});
      await page.locator('#canvas').press('Escape', {delay:80});
      await clickControl(page, 'quit');
      await screen(page, 'EndGameScreen');
      await page.locator('#canvas').press('Enter', {delay:80});
      await screen(page, 'CustomGameScreen');
      await clickControl(page, 'map/mode/1');
      await clickCustomGameStart(page);
      await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(25);
      await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
      expect(errors).toEqual([]);
    } finally {
      release();
    }
  });
}

test('an optional asset video worker failure keeps the match playable', async ({page}, info) => {
  test.setTimeout(240000);
  let abortWorker;
  const hold = new Promise(resolve => { abortWorker = resolve; });
  let requests = 0;
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.route(/\/asset-(?:[a-f0-9]+-)?video-worker\.js$/, async route => {
    ++requests;
    await hold;
    await route.abort();
  });
  await page.goto('/?renderer=webgl2&gl-errors=1&threads=serial');
  await screen(page, 'MainMenuScreen');
  const hasVideo = await page.evaluate(() => Module.glob2AssetManifest.packages.some(entry =>
    entry.parts.some(part => part.files.some(([path]) => path.endsWith('.h264')))));
  test.skip(!hasVideo, 'The WebP-only build has no asset video worker requests');
  try {
    await clickMainMenu(page, 'custom');
    await screen(page, 'CustomGameScreen');
    await clickCustomGameStart(page);
    await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(25);
    await expect.poll(() => requests, {timeout:120000}).toBeGreaterThan(0);
    const tick = (await state(page)).tick;
    abortWorker();
    await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(tick + 25);
    await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
    expect((await state(page)).renderContext.error).toBe(0);
    expect(errors).toEqual([]);
    await page.screenshot({path:info.outputPath('worker-failure-native-fallback.png')});
  } finally {
    abortWorker();
  }
});
