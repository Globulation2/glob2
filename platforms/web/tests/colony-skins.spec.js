const {waitForMainMenu}=require('./main-menu');
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
    globalThis.skinComposites = 0; globalThis.skinDraws = 0; globalThis.skinPasses = 0; globalThis.skinMeshCounts = [];
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
            if (event.data?.glob2SkinTestComposite) { event.stopImmediatePropagation(); globalThis.skinComposites++; }
            if (event.data?.glob2SkinTestDraw) {
              event.stopImmediatePropagation();
              record(event.data.count, event.data.epoch);
            }
          });
        }
      };
    }
    const atlases = new WeakSet();
    const shaders = new WeakSet(), programs = new WeakSet(), inspected = new WeakSet();
    const bindings = new WeakMap(), observed = new WeakSet();
    const state = gl => {
      if (!bindings.has(gl)) bindings.set(gl, {program:null, unit:gl.TEXTURE0, textures:new Map(), lost:false});
      if (!observed.has(gl)) {
        observed.add(gl);
        gl.canvas.addEventListener('webglcontextlost', () => {
          bindings.set(gl, {program:null, unit:gl.TEXTURE0, textures:new Map(), lost:true});
        });
        gl.canvas.addEventListener('webglcontextrestored', () => bindings.delete(gl));
      }
      return bindings.get(gl);
    };
    const proto = WebGL2RenderingContext.prototype;
    // Observe the actual binding calls. Querying GPU state for every ordinary
    // sprite draw can stall serial software-backed GL and starve persistence.
    const use = proto.useProgram, active = proto.activeTexture, texture = proto.bindTexture;
    proto.useProgram = function(program) {
      const result = use.call(this, program); state(this).program = program; return result;
    };
    proto.activeTexture = function(unit) {
      const result = active.call(this, unit); state(this).unit = unit; return result;
    };
    proto.bindTexture = function(target, value) {
      const result = texture.call(this, target, value);
      if (target === this.TEXTURE_2D) { const s=state(this); s.textures.set(s.unit, value); }
      return result;
    };
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
    const recordComposite = gl => {
      const s = state(gl);
      if (s.lost || !atlases.has(s.textures.get(s.unit))) return;
      if (typeof window === 'undefined') globalThis.postMessage({glob2SkinTestComposite:true});
      else globalThis.skinComposites++;
    };
    proto.drawElements = function(...args) {
      const s = state(this), program = s.lost ? null : s.program;
      if (program && !inspected.has(program)) {
        inspected.add(program);
        // Cross-check the observation once per program against the real GL
        // state, retaining independent evidence without per-sprite queries.
        if (this.getParameter(this.CURRENT_PROGRAM) !== program ||
            this.getParameter(this.TEXTURE_BINDING_2D) !== (s.textures.get(s.unit) || null))
          throw new Error('Skin tracker binding observation differs from WebGL state');
        if (this.getAttachedShaders(program)?.some(shader => this.getShaderSource(shader)?.includes('surfaceNormal'))) programs.add(program);
      }
      if (programs.has(program)) {
        const atlas = this.getFramebufferAttachmentParameter(this.FRAMEBUFFER, this.COLOR_ATTACHMENT0, this.FRAMEBUFFER_ATTACHMENT_OBJECT_NAME);
        if (atlas) atlases.add(atlas);
        if (typeof window === 'undefined') globalThis.postMessage({glob2SkinTestDraw:true,count:args[1],epoch:framebufferEpoch});
        else record(args[1],framebufferEpoch);
      } else recordComposite(this);
      return draw.apply(this,args);
    };
    const arrays = proto.drawArrays;
    proto.drawArrays = function(...args) {
      recordComposite(this);
      return arrays.apply(this,args);
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
  // colony-v2 preview pair: a 512x512 colour atlas and its material-id map.
  const fixture = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../../test/fixtures/skins/authorization.json'), 'utf8'));
  const paint = [...Buffer.from(fixture.textureHex, 'hex')];
  const material = [...Buffer.from(fixture.materialHex, 'hex')];
  // Use the current production shell even after a serial-only runtime build;
  // packaging index.html otherwise waits for the threaded runtime too.
  let shell = fs.readFileSync(path.resolve(__dirname, '../shell.html'), 'utf8')
    .replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>');
  shell = shell.replace('<head>', `<head><script>history.replaceState(null,'',location.pathname+'?renderer=webgl2&gl-errors=1&threads=${executionMode}');</script>`);
  shell = shell.replace('preRun: [function() {', `preRun: [function() {
    ENV.GLOB2_SKIN_PREVIEW_DIR='/data/skins/colony-v1';
    FS.mkdirTree('/data/skins/colony-v1');
    FS.writeFile('/data/skins/colony-v1/paint.webp',new Uint8Array(${JSON.stringify(paint)}));
    FS.writeFile('/data/skins/colony-v1/material.webp',new Uint8Array(${JSON.stringify(material)}));`);
  await openRuntimeHost(page, shell);
  const state = async () => {
    if (errors.length) throw new Error(errors.join('\n'));
    return page.evaluate(() => glob2Diagnostics.snapshot());
  };
  await waitForMainMenu(page);
  expect((await state()).renderer).toBe('webgl2');
  expect(await page.evaluate(() => Module.executionMode)).toBe(executionMode);
  await clickMainMenu(page, 'custom');
  await expect.poll(async () => (await state()).screen).toContain('CustomGameScreen');
  await clickCustomGameStart(page);
  await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('match');
  await expect.poll(() => page.evaluate(() => globalThis.skinDraws), {timeout:60000}).toBeGreaterThan(5);
  const manifest = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../../content/data/skins/colony-v1/manifest.json'), 'utf8'));
  for (const name of ['worker-walk.gsk', 'swarm.gsk'])
    await expect.poll(() => page.evaluate(() => globalThis.skinMeshCounts), {timeout:60000}).toContain(manifest.meshes[name].triangles * 3);
  expect((await state()).assets.skins).toBe('ready');
  expect((await state()).renderContext.error).toBe(0);
  await page.screenshot({path:info.outputPath('colony-skins-webgl2.png')});
  const batching = await page.evaluate(() => ({draws:globalThis.skinDraws,passes:globalThis.skinPasses}));
  expect(batching.draws).toBeGreaterThan(batching.passes);
  const before = await page.evaluate(() => globalThis.skinComposites);
  await page.evaluate(() => Module._glob2_context_action(0));
  await expect.poll(async () => (await state()).contextLost).toBe(true);
  await page.waitForTimeout(300);
  await page.evaluate(() => Module._glob2_context_action(1));
  await expect.poll(async () => (await state()).contextRestores, {timeout:30000}).toBeGreaterThan(0);
  await expect.poll(() => page.evaluate(() => globalThis.skinComposites), {timeout:30000}).toBeGreaterThan(before + 5);
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
  const hiddenDraws = await page.evaluate(() => globalThis.skinComposites);
  await page.waitForTimeout(500);
  expect(await page.evaluate(() => globalThis.skinComposites)).toBe(hiddenDraws);
  await page.screenshot({path:info.outputPath('colony-skins-hidden.png')});
  await page.locator('#canvas').press('Escape', {delay:80});
  await clickControl(page, 'options');
  await clickControl(page, 'colony-skins');
  await clickControl(page, 'ok');
  await expect.poll(() => page.evaluate(() => globalThis.skinComposites)).toBeGreaterThan(hiddenDraws + 5);
  await page.screenshot({path:info.outputPath('colony-skins-shown-again.png')});
  expect(errors).toEqual([]);
});
