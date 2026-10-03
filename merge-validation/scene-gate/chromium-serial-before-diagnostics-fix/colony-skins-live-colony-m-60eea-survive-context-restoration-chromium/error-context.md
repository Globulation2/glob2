# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: colony-skins.spec.js >> live colony meshes render through WebGL2 and survive context restoration
- Location: browser/tests/colony-skins.spec.js:9:1

# Error details

```
Error: expect(received).toBe(expected) // Object.is equality

Expected: 0
Received: null
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [aria-hidden]:
    - strong: Globulation 2
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 4.5 of 4.5 MB
  - generic "Globulation 2" [active] [ref=e2]
```

# Test source

```ts
  17  |   });
  18  |   const installTracking = () => {
  19  |     if (globalThis.__skinTrackingInstalled || typeof WebGL2RenderingContext === 'undefined') return;
  20  |     globalThis.__skinTrackingInstalled = true;
  21  |     globalThis.skinComposites = 0; globalThis.skinDraws = 0; globalThis.skinPasses = 0; globalThis.skinMeshCounts = [];
  22  |     let framebufferEpoch = 0, lastSkinEpoch = -1;
  23  |     const record = (count, epoch) => {
  24  |       globalThis.skinDraws++;
  25  |       if (lastSkinEpoch !== epoch) { globalThis.skinPasses++; lastSkinEpoch = epoch; }
  26  |       if (!globalThis.skinMeshCounts.includes(count)) globalThis.skinMeshCounts.push(count);
  27  |     };
  28  |     if (typeof window !== 'undefined') {
  29  |       const NativeWorker = globalThis.Worker;
  30  |       globalThis.Worker = class extends NativeWorker {
  31  |         constructor(...args) {
  32  |           super(...args);
  33  |           this.addEventListener('message', event => {
  34  |             if (event.data?.glob2SkinTestComposite) { event.stopImmediatePropagation(); globalThis.skinComposites++; }
  35  |             if (event.data?.glob2SkinTestDraw) {
  36  |               event.stopImmediatePropagation();
  37  |               record(event.data.count, event.data.epoch);
  38  |             }
  39  |           });
  40  |         }
  41  |       };
  42  |     }
  43  |     const atlases = new WeakSet();
  44  |     const shaders = new WeakSet(), programs = new WeakSet(), inspected = new WeakSet();
  45  |     const proto = WebGL2RenderingContext.prototype;
  46  |     const source = proto.shaderSource, attach = proto.attachShader, draw = proto.drawElements, bind = proto.bindFramebuffer;
  47  |     proto.bindFramebuffer = function(...args) { framebufferEpoch++; return bind.apply(this,args); };
  48  |     proto.shaderSource = function(shader, text) {
  49  |       if (text.includes('surfaceNormal')) shaders.add(shader);
  50  |       return source.call(this, shader, text);
  51  |     };
  52  |     proto.attachShader = function(program, shader) {
  53  |       if (shaders.has(shader)) programs.add(program);
  54  |       return attach.call(this, program, shader);
  55  |     };
  56  |     const recordComposite = gl => {
  57  |       if (!atlases.has(gl.getParameter(gl.TEXTURE_BINDING_2D))) return;
  58  |       if (typeof window === 'undefined') globalThis.postMessage({glob2SkinTestComposite:true});
  59  |       else globalThis.skinComposites++;
  60  |     };
  61  |     proto.drawElements = function(...args) {
  62  |       const program = this.getParameter(this.CURRENT_PROGRAM);
  63  |       if (program && !inspected.has(program)) {
  64  |         inspected.add(program);
  65  |         if (this.getAttachedShaders(program)?.some(shader => this.getShaderSource(shader)?.includes('surfaceNormal'))) programs.add(program);
  66  |       }
  67  |       if (programs.has(program)) {
  68  |         const atlas = this.getFramebufferAttachmentParameter(this.FRAMEBUFFER, this.COLOR_ATTACHMENT0, this.FRAMEBUFFER_ATTACHMENT_OBJECT_NAME);
  69  |         if (atlas) atlases.add(atlas);
  70  |         if (typeof window === 'undefined') globalThis.postMessage({glob2SkinTestDraw:true,count:args[1],epoch:framebufferEpoch});
  71  |         else record(args[1],framebufferEpoch);
  72  |       } else recordComposite(this);
  73  |       return draw.apply(this,args);
  74  |     };
  75  |     const arrays = proto.drawArrays;
  76  |     proto.drawArrays = function(...args) {
  77  |       recordComposite(this);
  78  |       return arrays.apply(this,args);
  79  |     };
  80  |   };
  81  |   await page.addInitScript(installTracking);
  82  |   if (executionMode === 'threaded') {
  83  |     // The application pthread owns its OffscreenCanvas and can block its event
  84  |     // loop. Install before its runtime starts; forward draw evidence to the UI.
  85  |     await page.context().route('**/threaded/index.js', async route => {
  86  |       const response = await route.fetch();
  87  |       await route.fulfill({response,body:`(${installTracking.toString()})();\n${await response.text()}`});
  88  |     });
  89  |   }
  90  |   const png = [...fs.readFileSync(path.resolve(__dirname, '../../platform/apps/api/assets/skins/stripes.png'))];
  91  |   // Use the current production shell even after a serial-only runtime build;
  92  |   // packaging index.html otherwise waits for the threaded runtime too.
  93  |   let shell = fs.readFileSync(path.resolve(__dirname, '../shell.html'), 'utf8')
  94  |     .replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>');
  95  |   shell = shell.replace('<head>', `<head><script>history.replaceState(null,'',location.pathname+'?renderer=webgl2&threads=${executionMode}');</script>`);
  96  |   shell = shell.replace('preRun: [function() {', `preRun: [function() {
  97  |     ENV.GLOB2_SKIN_PREVIEW_DIR='/data/skins/colony-v1';
  98  |     FS.mkdirTree('/data/skins/colony-v1');
  99  |     FS.writeFile('/data/skins/colony-v1/paint.png',new Uint8Array(${JSON.stringify(png)}));`);
  100 |   await openRuntimeHost(page, shell);
  101 |   const state = async () => {
  102 |     if (errors.length) throw new Error(errors.join('\n'));
  103 |     return page.evaluate(() => glob2Diagnostics.snapshot());
  104 |   };
  105 |   await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('MainMenuScreen');
  106 |   expect((await state()).renderer).toBe('webgl2');
  107 |   expect(await page.evaluate(() => Module.executionMode)).toBe(executionMode);
  108 |   await clickMainMenu(page, 'custom');
  109 |   await expect.poll(async () => (await state()).screen).toContain('CustomGameScreen');
  110 |   await clickCustomGameStart(page);
  111 |   await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('match');
  112 |   await expect.poll(() => page.evaluate(() => globalThis.skinDraws), {timeout:60000}).toBeGreaterThan(5);
  113 |   const manifest = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../data/skins/colony-v1/manifest.json'), 'utf8'));
  114 |   for (const name of ['worker-walk.gsk', 'swarm.gsk'])
  115 |     await expect.poll(() => page.evaluate(() => globalThis.skinMeshCounts), {timeout:60000}).toContain(manifest.meshes[name].triangles * 3);
  116 |   expect((await state()).assets.skins).toBe('ready');
> 117 |   expect((await state()).renderContext.error).toBe(0);
      |                                               ^ Error: expect(received).toBe(expected) // Object.is equality
  118 |   await page.screenshot({path:info.outputPath('colony-skins-webgl2.png')});
  119 |   const batching = await page.evaluate(() => ({draws:globalThis.skinDraws,passes:globalThis.skinPasses}));
  120 |   expect(batching.draws).toBeGreaterThan(batching.passes);
  121 |   const before = await page.evaluate(() => globalThis.skinComposites);
  122 |   await page.evaluate(() => Module._glob2_context_action(0));
  123 |   await expect.poll(async () => (await state()).contextLost).toBe(true);
  124 |   await page.waitForTimeout(300);
  125 |   await page.evaluate(() => Module._glob2_context_action(1));
  126 |   await expect.poll(async () => (await state()).contextRestores, {timeout:30000}).toBeGreaterThan(0);
  127 |   await expect.poll(() => page.evaluate(() => globalThis.skinComposites), {timeout:30000}).toBeGreaterThan(before + 5);
  128 |   expect((await state()).renderContext.error).toBe(0);
  129 |   await page.screenshot({path:info.outputPath('colony-skins-restored.png')});
  130 |   await page.locator('#canvas').press('Escape', {delay:80});
  131 |   await clickControl(page, 'options');
  132 |   await clickControl(page, 'colony-skins');
  133 |   await page.screenshot({path:info.outputPath('colony-skins-options.png')});
  134 |   await clickControl(page, 'ok');
  135 |   await expect.poll(async () => (await state()).screen).toContain('match');
  136 |   // Let any already-posted threaded draw evidence drain before checking that
  137 |   // classic sprites have completely replaced the mesh pass.
  138 |   await page.waitForTimeout(500);
  139 |   const hiddenDraws = await page.evaluate(() => globalThis.skinComposites);
  140 |   await page.waitForTimeout(500);
  141 |   expect(await page.evaluate(() => globalThis.skinComposites)).toBe(hiddenDraws);
  142 |   await page.screenshot({path:info.outputPath('colony-skins-hidden.png')});
  143 |   await page.locator('#canvas').press('Escape', {delay:80});
  144 |   await clickControl(page, 'options');
  145 |   await clickControl(page, 'colony-skins');
  146 |   await clickControl(page, 'ok');
  147 |   await expect.poll(() => page.evaluate(() => globalThis.skinComposites)).toBeGreaterThan(hiddenDraws + 5);
  148 |   await page.screenshot({path:info.outputPath('colony-skins-shown-again.png')});
  149 |   expect(errors).toEqual([]);
  150 | });
  151 | 
```