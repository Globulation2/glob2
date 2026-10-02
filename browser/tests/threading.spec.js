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


test('threaded audio produces PCM, keeps settings responsive and shuts down cleanly', async ({page}) => {
  await page.goto('/?renderer=software');
  await screen(page, 'MainMenuScreen');
  expect(await page.evaluate(() => Module.executionMode)).toBe('threaded');
  await clickMainMenu(page, 'settings'); await screen(page, 'SettingsScreen');
  await clickControl(page, 'nav.1'); await clickControl(page, 'audio.mute');
  await page.waitForFunction(() => Module.SDL2?.audio?.scriptProcessorNode);
  await page.evaluate(() => {
    window.audioBlocks = 0;
    const node = Module.SDL2.audio.scriptProcessorNode;
    const render = node.onaudioprocess;
    node.onaudioprocess = event => {
      render(event);
      if (event.outputBuffer.getChannelData(0).some(sample => Math.abs(sample) > 0.00001))
        ++window.audioBlocks;
    };
  });
  await expect.poll(() => page.evaluate(() => window.audioBlocks)).toBeGreaterThan(0);
  await clickControl(page, 'done'); await screen(page, 'MainMenuScreen');
  await clickMainMenu(page, 'quit'); await screen(page, 'exited');
});
