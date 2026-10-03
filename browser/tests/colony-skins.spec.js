const fs = require('node:fs');
const path = require('node:path');
const {test, expect} = require('@playwright/test');
const {openRuntimeHost} = require('./runtime-host');
const {clickMainMenu, clickCustomGameStart, clickControl} = require('./main-menu');

// Use the normal game shell and installed mesh assets. The preview switch is
// confined to this test host; production assignments use verified tickets.
test('live colony meshes render through WebGL2 and survive context restoration', async ({page}, info) => {
  test.setTimeout(240000);
  const executionMode = process.env.GLOB2_SKIN_TEST_THREADS || 'serial';
  if (!['serial', 'threaded'].includes(executionMode)) throw new Error('Unsupported skin test execution mode');
  const errors = [];
  page.on('pageerror', error => {errors.push(String(error)); console.error(String(error));});
  page.on('console', message => {
    if (/GL_INVALID|WebGL:.*(INVALID|error)|Aborted/.test(message.text())) errors.push(message.text());
  });
  const installTracking = () => {
    if (globalThis.__skinTrackingInstalled || typeof WebGL2RenderingContext === 'undefined') return;
    globalThis.__skinTrackingInstalled = true;
    globalThis.skinDraws = 0; globalThis.skinPasses = 0; globalThis.skinMeshCounts = [];
    let framebufferEpoch = 0, lastSkinEpoch = -1;
    const record = (count, epoch) => {
      globalThis.skinDraws++;
      if (lastSkinEpoch !== epoch) { globalThis.skinPasses++; lastSkinEpoch = epoch; }
      if (!globalThis.skinMeshCounts.includes(count)) globalThis.skinMeshCounts.push(count);
    };
    if (typeof window !== 'undefined') {
      const NativeWorker = globalThis.Worker;
      globalThis.Worker = class extends NativeWorker {
        constructor(...args) {
          super(...args);
          this.addEventListener('message', event => {
            if (event.data?.glob2SkinTestDraw) {
              event.stopImmediatePropagation();
              record(event.data.count, event.data.epoch);
            }
          });
        }
      };
    }
    const shaders = new WeakSet(), programs = new WeakSet(), inspected = new WeakSet();
    const proto = WebGL2RenderingContext.prototype;
    const source = proto.shaderSource, attach = proto.attachShader, draw = proto.drawElements, bind = proto.bindFramebuffer;
    proto.bindFramebuffer = function(...args) { framebufferEpoch++; return bind.apply(this,args); };
    proto.shaderSource = function(shader, text) {
      if (text.includes('surfaceNormal')) shaders.add(shader);
      return source.call(this, shader, text);
    };
    proto.attachShader = function(program, shader) {
      if (shaders.has(shader)) programs.add(program);
      return attach.call(this, program, shader);
    };
    proto.drawElements = function(...args) {
      const program = this.getParameter(this.CURRENT_PROGRAM);
      if (program && !inspected.has(program)) {
        inspected.add(program);
        if (this.getAttachedShaders(program)?.some(shader => this.getShaderSource(shader)?.includes('surfaceNormal'))) programs.add(program);
      }
      if (programs.has(program)) {
        if (typeof window === 'undefined') globalThis.postMessage({glob2SkinTestDraw:true,count:args[1],epoch:framebufferEpoch});
        else record(args[1],framebufferEpoch);
      }
      return draw.apply(this,args);
    };
  };
  await page.addInitScript(installTracking);
  if (executionMode === 'threaded') {
    // The application pthread owns its OffscreenCanvas and can block its event
    // loop. Install before its runtime starts; forward draw evidence to the UI.
    await page.context().route('**/threaded/index.js', async route => {
      const response = await route.fetch();
      await route.fulfill({response,body:`(${installTracking.toString()})();\n${await response.text()}`});
    });
  }
  const png = [...fs.readFileSync(path.resolve(__dirname, '../../platform/apps/api/assets/skins/stripes.png'))];
  // Use the current production shell even after a serial-only runtime build;
  // packaging index.html otherwise waits for the threaded runtime too.
  let shell = fs.readFileSync(path.resolve(__dirname, '../shell.html'), 'utf8')
    .replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>');
  shell = shell.replace('<head>', `<head><script>history.replaceState(null,'',location.pathname+'?renderer=webgl2&threads=${executionMode}');</script>`);
  shell = shell.replace('preRun: [function() {', `preRun: [function() {
    ENV.GLOB2_SKIN_PREVIEW_DIR='/data/skins/colony-v1';
    FS.mkdirTree('/data/skins/colony-v1');
    FS.writeFile('/data/skins/colony-v1/paint.png',new Uint8Array(${JSON.stringify(png)}));`);
  await openRuntimeHost(page, shell);
  const state = async () => {
    if (errors.length) throw new Error(errors.join('\n'));
    return page.evaluate(() => glob2Diagnostics.snapshot());
  };
  await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('MainMenuScreen');
  expect((await state()).renderer).toBe('webgl2');
  expect(await page.evaluate(() => Module.executionMode)).toBe(executionMode);
  await clickMainMenu(page, 'custom');
  await expect.poll(async () => (await state()).screen).toContain('CustomGameScreen');
  await clickCustomGameStart(page);
  await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('match');
  await page.evaluate(() => {globalThis.skinDraws=0;globalThis.skinPasses=0;globalThis.skinMeshCounts=[];});
  await expect.poll(() => page.evaluate(() => globalThis.skinDraws), {timeout:60000}).toBeGreaterThan(5);
  const manifest = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../data/skins/colony-v1/manifest.json'), 'utf8'));
  for (const name of ['worker-walk.gsk', 'swarm.gsk'])
    await expect.poll(() => page.evaluate(() => globalThis.skinMeshCounts), {timeout:60000}).toContain(manifest.meshes[name].triangles * 3);
  expect((await state()).assets.skins).toBe('ready');
  expect((await state()).renderContext.error).toBe(0);
  await page.screenshot({path:info.outputPath('colony-skins-webgl2.png')});
  const batching = await page.evaluate(() => ({draws:globalThis.skinDraws,passes:globalThis.skinPasses}));
  expect(batching.draws).toBeGreaterThan(batching.passes);
  const before = await page.evaluate(() => globalThis.skinDraws);
  await page.evaluate(() => Module._glob2_context_action(0));
  await expect.poll(async () => (await state()).contextLost).toBe(true);
  await page.waitForTimeout(300);
  await page.evaluate(() => Module._glob2_context_action(1));
  await expect.poll(async () => (await state()).contextRestores, {timeout:30000}).toBeGreaterThan(0);
  await expect.poll(() => page.evaluate(() => globalThis.skinDraws), {timeout:30000}).toBeGreaterThan(before + 5);
  expect((await state()).renderContext.error).toBe(0);
  await page.screenshot({path:info.outputPath('colony-skins-restored.png')});
  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'options');
  await clickControl(page, 'colony-skins');
  await page.screenshot({path:info.outputPath('colony-skins-options.png')});
  await clickControl(page, 'ok');
  await expect.poll(async () => (await state()).screen).toContain('match');
  // Let any already-posted threaded draw evidence drain before checking that
  // classic sprites have completely replaced the mesh pass.
  await page.waitForTimeout(500);
  const hiddenDraws = await page.evaluate(() => globalThis.skinDraws);
  await page.waitForTimeout(500);
  expect(await page.evaluate(() => globalThis.skinDraws)).toBe(hiddenDraws);
  await page.screenshot({path:info.outputPath('colony-skins-hidden.png')});
  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'options');
  await clickControl(page, 'colony-skins');
  await clickControl(page, 'ok');
  await expect.poll(() => page.evaluate(() => globalThis.skinDraws)).toBeGreaterThan(hiddenDraws + 5);
  await page.screenshot({path:info.outputPath('colony-skins-shown-again.png')});
  expect(errors).toEqual([]);
});
