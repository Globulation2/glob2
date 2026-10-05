const {test,expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const {gameURL,clickMainMenu,clickControl} = require('./main-menu');
const {openRuntimeHost} = require('./runtime-host');
const screen=(page,name)=>expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().screen)).toContain(name);
for (const variant of ['threaded','serial']) {
test(`${variant} runtime executes shared compute, gradients and save lifecycle tests`,async({page},info)=>{
  const evidence=path.resolve(__dirname,'../../artifacts/browser-determinism/worker-lifecycle');
  fs.mkdirSync(evidence,{recursive:true});
  const log=path.join(evidence,`${info.project.name}-${variant}.log`);
  fs.writeFileSync(log,'');
  page.on('console',message=>fs.appendFileSync(log,message.text()+'\n'));
  page.on('pageerror',error=>fs.appendFileSync(log,String(error)+'\n'));
  const url = new URL(gameURL(),'http://localhost');
  if (variant==='serial') url.searchParams.set('threads','serial');
  await page.goto(url.pathname+url.search);
  await screen(page,'MainMenuScreen');
  expect(await page.evaluate(()=>glob2Diagnostics.snapshot().executionMode)).toBe(variant);
  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickControl(page,'map/mode/1');
  await clickControl(page,'generator/landscape'); await screen(page,'LandscapePickerScreen');
  if (variant==='threaded')
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().workerCount)).toBeGreaterThan(0);
  else expect(await page.evaluate(()=>glob2Diagnostics.snapshot().workerCount)).toBe(0);
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'CustomGameScreen');
  // The shared harness proves overlapping execution and distinct thread IDs.
  await openRuntimeHost(page, `<canvas id="canvas"></canvas><script>
      var Module={noInitialRun:true,canvas:document.querySelector('canvas'),
        locateFile:name=>name.endsWith('.data')?'/'+name:'/${variant==='threaded'?'threaded/':''}'+name,
        async onRuntimeInitialized(){
          window.heartbeats=0;const timer=setInterval(()=>++window.heartbeats,10);
          window.result=await Module.start(['--test-suite=ComputeExecutor,GradientPipeline,SharedWorkerLifecycle,BuildingGradientInvalidation,PathGradient']);
          clearInterval(timer);
        }};
    </script><script src="/${variant==='threaded'?'threaded/':''}script-tests.js"></script>`);
  await page.waitForFunction(()=>window.result!==undefined);
  expect(await page.evaluate(()=>window.result)).toBe(0);
  if (variant==='threaded') expect(await page.evaluate(()=>window.heartbeats)).toBeGreaterThan(0);
});
}
test('serial selection preserves ordinary startup',async({page})=>{
  const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads','serial');
  await page.goto(url.pathname+url.search);
  await screen(page,'MainMenuScreen');
  expect(await page.evaluate(()=>glob2Diagnostics.snapshot().executionMode)).toBe('serial');
  expect(await page.evaluate(()=>glob2Diagnostics.snapshot().workerCount)).toBe(0);
});
test('missing isolation headers automatically selects serial',async({page})=>{
  await page.route('**/index.html*',async route=>{
    const response=await route.fetch();const headers=response.headers();
    delete headers['cross-origin-opener-policy'];delete headers['cross-origin-embedder-policy'];
    await route.fulfill({response,headers});
  });
  await page.goto('/index.html?renderer=software');
  await screen(page,'MainMenuScreen');
  const state=await page.evaluate(()=>glob2Diagnostics.snapshot());
  expect(state.executionMode).toBe('serial');expect(state.threadFallback).toMatch(/isolation/);
});

test('threaded startup failure reloads the serial runtime',async({page})=>{
  await page.route('**/threaded/index.js',route=>route.fulfill({contentType:'application/javascript',
    body:'Module.onAbort("test startup failure");'}));
  await page.goto(gameURL());
  await page.waitForURL(url=>url.searchParams.get('threads')==='serial');
  await screen(page,'MainMenuScreen');
  const state=await page.evaluate(()=>glob2Diagnostics.snapshot());
  expect(state.executionMode).toBe('serial');
  expect(state.threadFallback).toContain('test startup failure');
});

test('a real pthread worker error after the probe reloads the serial runtime', async ({page}) => {
  await page.addInitScript(() => {
    const NativeWorker = window.Worker;
    window.Worker = class extends NativeWorker {
      constructor(url, options) {
        if (String(url).includes('/threaded/index')) {
          url = URL.createObjectURL(new Blob([
            'throw new Error("test pthread startup failure");'
          ], {type:'text/javascript'}));
        }
        super(url, options);
      }
    };
  });
  await page.goto(gameURL());
  await page.waitForURL(url => url.searchParams.get('threads') === 'serial');
  await screen(page, 'MainMenuScreen');
  const state = await page.evaluate(() => glob2Diagnostics.snapshot());
  expect(state.executionMode).toBe('serial');
  expect(state.threadFallback).toContain('test pthread startup failure');
});


for (const variant of ['serial', 'threaded']) {
test(`${variant} audio produces PCM, keeps settings responsive and shuts down cleanly`, async ({page}, info) => {
  await page.goto(`/?renderer=software&threads=${variant}`);
  await screen(page, 'MainMenuScreen');
  expect(await page.evaluate(() => Module.executionMode)).toBe(variant);
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().assets['menu-music'])).toBe('ready');
  expect(await page.evaluate(() => FS.analyzePath('/data/zik/menu.opus').exists)).toBe(true);
  expect(await page.evaluate(() => FS.analyzePath('/data/zik/menu.ogg').exists)).toBe(false);
  await clickMainMenu(page, 'settings'); await screen(page, 'SettingsScreen');
  await clickControl(page, 'nav.1'); await clickControl(page, 'audio.mute');
  await page.waitForFunction(() => Module.glob2Music?.ready && Module.glob2Music.node);
  await page.evaluate(() => {
    const {context, node} = Module.glob2Music;
    window.audioContext = context;
    window.audioAnalyser = context.createAnalyser();
    window.audioSamples = new Float32Array(window.audioAnalyser.fftSize);
    // Inspect the PCM that reaches the output, including the worklet's gain.
    node.disconnect(context.destination);
    node.connect(window.audioAnalyser);
    window.audioAnalyser.connect(context.destination);
  });
  const peak = () => page.evaluate(() => {
    window.audioAnalyser.getFloatTimeDomainData(window.audioSamples);
    return window.audioSamples.reduce((maximum, sample) => Math.max(maximum, Math.abs(sample)), 0);
  });
  await expect.poll(peak).toBeGreaterThan(0.00001);
  await clickControl(page, 'audio.mute');
  await expect.poll(() => page.evaluate(() => Module.glob2Music.gain)).toBe(0);
  await expect.poll(peak).toBe(0);
  await clickControl(page, 'audio.mute');
  await expect.poll(peak).toBeGreaterThan(0.00001);
  await info.attach('audio-output-diagnostics', {
    body: JSON.stringify(await page.evaluate(() => ({
      executionMode: Module.executionMode,
      contextState: Module.glob2Music.context.state,
      status: Module.glob2Music.status,
      peak: window.audioSamples.reduce((maximum, sample) => Math.max(maximum, Math.abs(sample)), 0),
    }))),
    contentType: 'application/json',
  });
  await clickControl(page, 'done'); await screen(page, 'MainMenuScreen');
  await clickMainMenu(page, 'quit'); await screen(page, 'exited');
  await expect.poll(() => page.evaluate(() => window.audioContext.state)).toBe('closed');
  expect(await page.evaluate(() => Module.glob2Music.closed)).toBe(true);
});
}
