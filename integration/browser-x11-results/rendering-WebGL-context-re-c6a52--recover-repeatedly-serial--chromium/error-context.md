# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: rendering.spec.js >> WebGL context restoration keeps the match and can recover repeatedly (serial)
- Location: tests/rendering.spec.js:84:1

# Error details

```
Error: expect(received).toContain(expected) // indexOf

Expected substring: "CustomGameScreen"
Received string:    "14MainMenuScreen"

Call Log:
- Timeout 60000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [aria-hidden]:
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 42 of 42 MB
    - combobox
  - generic "Globulation 2" [active] [ref=e2]
```

# Test source

```ts
  1   | const {clickCreateMap}=require('./editor-controls');
  2   | const {test, expect} = require('@playwright/test');
  3   | const {clickMainMenu,clickSettingsDone,clickCustomGameStart,clickControl,control}=require('./main-menu');
  4   | const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
> 5   | const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
      |                                                                                    ^ Error: expect(received).toContain(expected) // indexOf
  6   | 
  7   | test('WebGL2 draws a playable match and resizes its drawing buffer', async ({page}, info) => {
  8   |   test.setTimeout(120000);
  9   |   const errors = [];
  10  |   page.on('pageerror', error => errors.push(String(error)));
  11  |   page.on('console', message => { if (/GL_INVALID|GL_INVALID_OPERATION|WebGL:.*(INVALID|error)|GPU mip preparation failed|Aborted/.test(message.text())) errors.push(message.text()); });
  12  |   await page.goto('/?renderer=webgl2&gl-errors=1');
  13  |   await screen(page, 'MainMenuScreen');
  14  |   expect((await state(page)).renderer).toBe('webgl2');
  15  |   expect((await state(page)).renderContext.width).toBeGreaterThan(0);
  16  |   await clickMainMenu(page,'custom'); await screen(page, 'CustomGameScreen');
  17  |   await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  18  |   await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(25);
  19  |   await page.setViewportSize({width:1280,height:720});
  20  |   await expect.poll(async () => { const s = await state(page); return [s.width,s.height]; }).toEqual([1280,720]);
  21  |   await expect.poll(async () => { const gl=(await state(page)).renderContext; return [gl.width,gl.height,gl.error]; }).toEqual([1280,720,0]);
  22  |   await expect.poll(() => require('./pixels').hasRenderedPixels(page)).toBe(true);
  23  |   await page.screenshot({path:info.outputPath('webgl2-match.png')});
  24  |   await page.locator('#canvas').press('Escape',{delay:80});
  25  |   await clickControl(page,'quit'); await screen(page,'EndGameScreen');
  26  |   expect(errors).toEqual([]);
  27  | });
  28  | 
  29  | test('software renderer remains available', async ({page}) => {
  30  |   await page.goto('/?renderer=software');
  31  |   await screen(page,'MainMenuScreen');
  32  |   expect((await state(page)).renderer).toBe('software');
  33  |   expect(await page.evaluate(() => Boolean(document.querySelector('#canvas').getContext('2d')))).toBe(true);
  34  |   await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  35  | });
  36  | 
  37  | // Test browsers usually emulate WebGL2. These tests answer the shell's acceleration
  38  | // probe at the browser boundary; its renderer selection runs unchanged.
  39  | const UNMASKED_RENDERER_WEBGL = 0x9246;
  40  | test('the default renderer is WebGL2 when the browser accelerates it', async ({page}) => {
  41  |   await page.addInitScript(name => {
  42  |     const getContext = HTMLCanvasElement.prototype.getContext;
  43  |     HTMLCanvasElement.prototype.getContext = function(type, attributes) {
  44  |       return getContext.call(this, type, type === 'webgl2' ? {...attributes, failIfMajorPerformanceCaveat:false} : attributes);
  45  |     };
  46  |     const getParameter = WebGL2RenderingContext.prototype.getParameter;
  47  |     WebGL2RenderingContext.prototype.getParameter = function(parameter) {
  48  |       return parameter === name ? 'Test hardware GPU' : getParameter.call(this, parameter);
  49  |     };
  50  |   }, UNMASKED_RENDERER_WEBGL);
  51  |   await page.goto('/'); await screen(page,'MainMenuScreen');
  52  |   expect((await state(page)).renderer).toBe('webgl2');
  53  |   expect((await state(page)).renderContext.width).toBeGreaterThan(0);
  54  |   expect((await state(page)).renderContext.error).toBeNull();
  55  | });
  56  | 
  57  | const fallbacks = {
  58  |   unavailable: () => {
  59  |     const getContext = HTMLCanvasElement.prototype.getContext;
  60  |     HTMLCanvasElement.prototype.getContext = function(type, attributes) {
  61  |       return type === 'webgl2' ? null : getContext.call(this, type, attributes);
  62  |     };
  63  |   },
  64  |   emulated: name => {
  65  |     const getParameter = WebGL2RenderingContext.prototype.getParameter;
  66  |     WebGL2RenderingContext.prototype.getParameter = function(parameter) {
  67  |       return parameter === name ? 'ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (LLVM 10.0.0)), SwiftShader driver)'
  68  |         : getParameter.call(this, parameter);
  69  |     };
  70  |   },
  71  | };
  72  | for (const [reason, stub] of Object.entries(fallbacks)) {
  73  |   test(`the default renderer falls back to software when WebGL2 is ${reason}`, async ({page}) => {
  74  |     await page.addInitScript(stub, UNMASKED_RENDERER_WEBGL);
  75  |     await page.goto('/'); await screen(page,'MainMenuScreen');
  76  |     expect((await state(page)).renderer).toBe('software');
  77  |     expect(await page.evaluate(() => Boolean(document.querySelector('#canvas').getContext('2d')))).toBe(true);
  78  |     await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  79  |   });
  80  | }
  81  | 
  82  | 
  83  | for (const variant of ['serial', 'threaded']) {
  84  | test(`WebGL context restoration keeps the match and can recover repeatedly (${variant})`, async ({page}, info) => {
  85  |   test.setTimeout(240000);
  86  |   const errors = [];
  87  |   page.on('pageerror', error => errors.push(String(error)));
  88  |   await page.goto(`/?renderer=webgl2&gl-errors=1&threads=${variant}`); await screen(page,'MainMenuScreen');
  89  |   await expect.poll(async () => (await state(page)).assets.hd, {timeout:120000}).toBe('ready');
  90  |   await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  91  |   await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  92  |   await expect.poll(async () => (await state(page)).tick, {timeout:60000}).toBeGreaterThan(25);
  93  |   await expect.poll(async () => (await state(page)).artworkReady, {timeout:120000}).toBe(true);
  94  |   for (let count=1; count<=2; ++count) {
  95  |     await page.evaluate(() => {
  96  |       Module._glob2_context_action(0);
  97  |     });
  98  |     await expect.poll(async () => (await state(page)).contextLost).toBe(true);
  99  |     const suspended = await state(page);
  100 |     const paused = suspended.tick;
  101 |     if (count === 1) await page.evaluate(() => {
  102 |       Object.defineProperty(document, 'hidden', {configurable:true, get:() => true});
  103 |       document.dispatchEvent(new Event('visibilitychange'));
  104 |     });
  105 |     const viewport = count === 1 ? {width:1280,height:720} : {width:1200,height:900};
```