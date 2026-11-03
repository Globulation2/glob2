// SPDX-License-Identifier: GPL-3.0-or-later
const {test,expect}=require('@playwright/test');
const {gameURL,waitForMainMenu,clickMainMenu,clickControl,clickByLabel}=require('./main-menu');
const {presentationWebGL}=require('./renderer-support');
for(const mode of ['serial','threaded'])test(`${mode}: renderer availability matches its real presentation realm and unsupported GL retains the session`,async({page},info)=>{
  test.setTimeout(180000);
  const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',mode);url.searchParams.set('renderer','software');
  await page.goto(url.pathname+url.search);await waitForMainMenu(page);
  test.skip(await page.evaluate(()=>glob2Diagnostics.snapshot().executionMode)!==mode,'Requested application execution mode unavailable');
  const supported=await presentationWebGL(page);
  await clickMainMenu(page,'settings');
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().screen)).toContain('SettingsScreen');
  const fullscreen=await page.evaluate(()=>document.fullscreenEnabled);
  if(fullscreen){
    await page.locator('#canvas').click({position:{x:5,y:5}});
    await page.evaluate(()=>Module.glob2PresentationSurface.fullscreenHost.requestFullscreen());
    await expect.poll(()=>page.evaluate(()=>document.fullscreenElement===Module.glob2PresentationSurface.fullscreenHost)).toBe(true);
  }
  await page.evaluate(()=>{globalThis.webglAvailabilityBefore={module:Module,canvas:Module.canvas,starts:Module.glob2ApplicationStarts,thread:Module.glob2ApplicationThreadIdentity,generation:Module.glob2PresentationSurface.generation};});
  const loop=await page.evaluate(()=>glob2Diagnostics.snapshot().loop);
  await clickControl(page,'graphics.renderer');
  const options=await page.evaluate(()=>Object.entries(glob2Diagnostics.snapshot().controls).filter(([key,value])=>/^popup\//.test(key)&&value.label.startsWith('Programmable GL')).map(([,value])=>value.label));
  expect(options).toHaveLength(1);
  expect(options[0]==='Programmable GL','Advertised availability must match an independent browser API probe').toBe(supported);
  if(supported)await page.locator('#canvas').press('Escape',{delay:80});
  else {
    expect(options[0]).toContain('Renderer unavailable');
    await clickByLabel(page,/popup/,options[0]);
    await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().controls['graphics.renderer']?.label)).toBe(options[0]);
    await page.screenshot({path:info.outputPath('unavailable-presentation-webgl.png')});
  }
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().loop)).toBeGreaterThan(loop);
  expect(await page.evaluate(()=>Module.renderer)).toBe('software');
  expect(await page.evaluate(()=>webglAvailabilityBefore.module===Module&&webglAvailabilityBefore.canvas===Module.canvas&&webglAvailabilityBefore.starts===Module.glob2ApplicationStarts&&webglAvailabilityBefore.thread===Module.glob2ApplicationThreadIdentity&&webglAvailabilityBefore.generation===Module.glob2PresentationSurface.generation)).toBe(true);
  if(fullscreen){expect(await page.evaluate(()=>document.fullscreenElement===Module.glob2PresentationSurface.fullscreenHost)).toBe(true);await page.evaluate(()=>document.exitFullscreen());}
});
