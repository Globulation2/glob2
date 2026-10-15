# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: threading.spec.js >> threaded runtime executes shared compute, gradients and save lifecycle tests
- Location: tests/threading.spec.js:8:1

# Error details

```
Error: expect(received).toContain(expected) // indexOf

Expected substring: "MainMenuScreen"
Received string:    "loading"

Call Log:
- Timeout 30000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [ref=e2]:
    - img "Globulation 2" [ref=e3]
    - status [ref=e6]: Starting the game…
    - generic [ref=e7]:
      - progressbar "Download progress" [ref=e8]
      - generic [ref=e10]: 33 of 33 MB
    - combobox "Language" [ref=e11]:
      - option "العربية"
      - option "Português brasileiro"
      - option "Català"
      - option "Čeština"
      - option "Deutsch"
      - option "Dansk"
      - option "English" [selected]
      - option "Español"
      - option "Esperanto"
      - option "Euskara"
      - option "فارسی"
      - option "Français"
      - option "Ελληνικά"
      - option "Magyar"
      - option "Italiano"
      - option "Nederlands"
      - option "Polski"
      - option "Português europeu"
      - option "Română"
      - option "Русский"
      - option "Slovenščina"
      - option "Slovenčina"
      - option "Српски"
      - option "Suomi"
      - option "Svenska"
      - option "Türkçe"
      - option "繁體中文"
      - option "简体中文"
      - option "Українська"
      - option "Bahasa Indonesia"
      - option "Tiếng Việt"
      - option "日本語"
      - option "한국어"
  - generic "Globulation 2" [active] [ref=e13]
```

# Test source

```ts
  1   | const {test,expect} = require('@playwright/test');
  2   | const fs = require('node:fs');
  3   | const path = require('node:path');
  4   | const {gameURL,clickMainMenu,clickControl} = require('./main-menu');
  5   | const {openRuntimeHost} = require('./runtime-host');
> 6   | const screen=(page,name)=>expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().screen)).toContain(name);
      |                                                                                                  ^ Error: expect(received).toContain(expected) // indexOf
  7   | for (const variant of ['threaded','serial']) {
  8   | test(`${variant} runtime executes shared compute, gradients and save lifecycle tests`,async({page},info)=>{
  9   |   // This compiled corpus includes millions of assertions and save/load cycles.
  10  |   test.setTimeout(300000);
  11  |   const evidence=path.resolve(__dirname,'../../artifacts/browser-determinism/worker-lifecycle');
  12  |   fs.mkdirSync(evidence,{recursive:true});
  13  |   const log=path.join(evidence,`${info.project.name}-${variant}.log`);
  14  |   fs.writeFileSync(log,'');
  15  |   page.on('console',message=>fs.appendFileSync(log,message.text()+'\n'));
  16  |   page.on('pageerror',error=>fs.appendFileSync(log,String(error)+'\n'));
  17  |   const url = new URL(gameURL(),'http://localhost');
  18  |   if (variant==='serial') url.searchParams.set('threads','serial');
  19  |   await page.goto(url.pathname+url.search);
  20  |   await screen(page,'MainMenuScreen');
  21  |   expect(await page.evaluate(()=>glob2Diagnostics.snapshot().executionMode)).toBe(variant);
  22  |   await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  23  |   await clickControl(page,'map/mode/1');
  24  |   await clickControl(page,'generator/landscape'); await screen(page,'LandscapePickerScreen');
  25  |   if (variant==='threaded')
  26  |     await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().workerCount)).toBeGreaterThan(0);
  27  |   else expect(await page.evaluate(()=>glob2Diagnostics.snapshot().workerCount)).toBe(0);
  28  |   await page.locator('#canvas').press('Escape',{delay:80});
  29  |   await screen(page,'CustomGameScreen');
  30  |   // The shared harness proves overlapping execution and distinct thread IDs.
  31  |   await openRuntimeHost(page, `<canvas id="canvas"></canvas><script>
  32  |       var Module={noInitialRun:true,canvas:document.querySelector('canvas'),
  33  |         locateFile:name=>name.endsWith('.data')?'/'+name:'/${variant==='threaded'?'threaded/':''}'+name,
  34  |         async onRuntimeInitialized(){
  35  |           window.heartbeats=0;const timer=setInterval(()=>++window.heartbeats,10);
  36  |           window.result=await Module.start(['--test-suite=EntityRandom,EntityRandomLifecycle,LegacyScriptCoverage,ComputeExecutor,ApplicationResourcePlan,ReplayTelemetry,GradientPipeline,SharedWorkerLifecycle,SimulationReadPhase,WorldSnapshot,SceneBuffer,SceneExtract,OverlayFill,BuildingGradientInvalidation,PathGradient','--test-case-exclude=*[display*']);
  37  |           clearInterval(timer);
  38  |         }};
  39  |     </script><script src="/${variant==='threaded'?'threaded/':''}script-tests.js"></script>`);
  40  |   await page.waitForFunction(()=>window.result!==undefined);
  41  |   expect(await page.evaluate(()=>window.result)).toBe(0);
  42  |   if (variant==='threaded') expect(await page.evaluate(()=>window.heartbeats)).toBeGreaterThan(0);
  43  | });
  44  | }
  45  | test('serial selection preserves ordinary startup',async({page})=>{
  46  |   const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads','serial');
  47  |   await page.goto(url.pathname+url.search);
  48  |   await screen(page,'MainMenuScreen');
  49  |   expect(await page.evaluate(()=>glob2Diagnostics.snapshot().executionMode)).toBe('serial');
  50  |   expect(await page.evaluate(()=>glob2Diagnostics.snapshot().workerCount)).toBe(0);
  51  | });
  52  | test('missing isolation headers automatically selects serial',async({page})=>{
  53  |   await page.route('**/index.html*',async route=>{
  54  |     const response=await route.fetch();const headers=response.headers();
  55  |     delete headers['cross-origin-opener-policy'];delete headers['cross-origin-embedder-policy'];
  56  |     await route.fulfill({response,headers});
  57  |   });
  58  |   await page.goto('/index.html?renderer=software');
  59  |   await screen(page,'MainMenuScreen');
  60  |   const state=await page.evaluate(()=>glob2Diagnostics.snapshot());
  61  |   expect(state.executionMode).toBe('serial');expect(state.threadFallback).toMatch(/isolation/);
  62  | });
  63  | 
  64  | test('threaded startup failure reloads the serial runtime',async({page})=>{
  65  |   await page.route('**/threaded/index.js',route=>route.fulfill({contentType:'application/javascript',
  66  |     body:'Module.onAbort("test startup failure");'}));
  67  |   await page.goto(gameURL());
  68  |   await page.waitForURL(url=>url.searchParams.get('threads')==='serial');
  69  |   await screen(page,'MainMenuScreen');
  70  |   const state=await page.evaluate(()=>glob2Diagnostics.snapshot());
  71  |   expect(state.executionMode).toBe('serial');
  72  |   expect(state.threadFallback).toContain('test startup failure');
  73  | });
  74  | 
  75  | test('a real pthread worker error after the probe reloads the serial runtime', async ({page}) => {
  76  |   await page.addInitScript(() => {
  77  |     const NativeWorker = window.Worker;
  78  |     window.Worker = class extends NativeWorker {
  79  |       constructor(url, options) {
  80  |         if (String(url).includes('/threaded/index')) {
  81  |           url = URL.createObjectURL(new Blob([
  82  |             'throw new Error("test pthread startup failure");'
  83  |           ], {type:'text/javascript'}));
  84  |         }
  85  |         super(url, options);
  86  |       }
  87  |     };
  88  |   });
  89  |   await page.goto(gameURL());
  90  |   await page.waitForURL(url => url.searchParams.get('threads') === 'serial');
  91  |   await screen(page, 'MainMenuScreen');
  92  |   const state = await page.evaluate(() => glob2Diagnostics.snapshot());
  93  |   expect(state.executionMode).toBe('serial');
  94  |   expect(state.threadFallback).toContain('test pthread startup failure');
  95  | });
  96  | 
  97  | 
  98  | for (const variant of ['serial', 'threaded']) {
  99  | test(`${variant} audio produces PCM, keeps settings responsive and shuts down cleanly`, async ({page}, info) => {
  100 |   await page.goto(`/?renderer=software&threads=${variant}`);
  101 |   await screen(page, 'MainMenuScreen');
  102 |   expect(await page.evaluate(() => Module.executionMode)).toBe(variant);
  103 |   await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().assets['menu-music'])).toBe('ready');
  104 |   expect(await page.evaluate(() => FS.analyzePath('/data/zik/menu.opus').exists)).toBe(true);
  105 |   expect(await page.evaluate(() => FS.analyzePath('/data/zik/menu.ogg').exists)).toBe(false);
  106 |   await clickMainMenu(page, 'settings'); await screen(page, 'SettingsScreen');
```