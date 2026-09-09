const {test, expect, chromium} = require('@playwright/test');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {gameURL} = require('../tests/game-url');

// Playwright's normal focus override keeps background documents visible.
// Use a real browser window and the default context without that override.
// Linux runners require a display (for example xvfb-run).
test('background single-player suspends and returns without catching up', async ({baseURL}, info) => {
  const profile = await fs.mkdtemp(path.join(os.tmpdir(),'glob2-visibility-'));
  // Linux CI lacks user namespaces and a hardware GPU. Give this local test
  // window the same sandbox policy as Playwright and an explicit software GPU;
  // these flags are never supplied to players' browsers.
  const ciArgs = process.env.CI && process.platform === 'linux'
    ? ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] : [];
  const child = spawn(chromium.executablePath(), ['--remote-debugging-port=0',
    '--user-data-dir='+profile, '--no-first-run', '--no-default-browser-check', ...ciArgs, 'about:blank'],
    {stdio:['ignore','ignore','pipe']});
  let launchError, launchLog = '';
  child.stderr.on('data', chunk => { launchLog = (launchLog + chunk).slice(-4000); });
  const exited = new Promise(resolve => {
    child.once('exit',resolve);
    child.once('error',error => { launchError = error; resolve(); });
  });
  let browser, page;
  try {
    let port;
    await expect.poll(async () => {
      if (launchError) throw launchError;
      if (child.exitCode !== null || child.signalCode !== null)
        throw new Error('Chromium exited before its debug port opened: ' + launchLog);
      try { port=(await fs.readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0]; return true; }
      catch { return false; }
    }).toBe(true);
    browser = await chromium.connectOverCDP('http://127.0.0.1:'+port,{noDefaults:true});
    const context=browser.contexts()[0]; page=context.pages()[0];
    await page.setViewportSize({width:1200,height:900});
    const snapshot=()=>page.evaluate(()=>glob2Diagnostics.snapshot());
    const screen=name=>expect.poll(async ()=>(await snapshot()).screen).toContain(name);
    const click=async (x,y)=>{
      const s=await snapshot(), box=await page.locator('#canvas').boundingBox();
      await page.locator('#canvas').click({position:{x:(x+(s.width-640)/2)*box.width/s.width,
        y:(y+(s.height-480)/2)*box.height/s.height},delay:80});
    };
    await page.bringToFront();
    await page.goto(new URL(gameURL(), baseURL).href); await screen('MainMenuScreen');
    await page.evaluate(()=>{
      window.visibilityInputs=[];
      for (const type of ['mousedown','mouseup','keydown','keyup'])
        document.addEventListener(type,event=>visibilityInputs.push({type,x:event.clientX,y:event.clientY,key:event.key,
          hidden:document.hidden,focus:document.hasFocus(),screen:glob2Diagnostics.snapshot().screen}),true);
    });
    if (process.env.GLOB2_TEST_RENDERER)
      expect((await snapshot()).renderer).toBe(process.env.GLOB2_TEST_RENDERER);
    await click(480,200); await screen('CustomGameScreen');
    await click(100,70); await click(530,380);
    await expect.poll(async ()=>(await snapshot()).tick).toBeGreaterThan(25);
    const other=await context.newPage();
    await other.bringToFront();
    await expect.poll(()=>page.evaluate(()=>document.visibilityState)).toBe('hidden');
    // Observe a full second after the host has had a callback to suspend.
    await expect.poll(async () => {
      const before=await snapshot();
      await other.evaluate(()=>new Promise(resolve=>setTimeout(resolve,1000)));
      return (await snapshot()).tick===before.tick;
    }).toBe(true);
    const hidden=await snapshot();
    await other.evaluate(()=>new Promise(resolve=>setTimeout(resolve,2000)));
    expect((await snapshot()).tick).toBe(hidden.tick);
    await page.bringToFront();
    await expect.poll(()=>page.evaluate(()=>document.visibilityState)).toBe('visible');
    await expect.poll(async ()=>(await snapshot()).tick).toBeGreaterThan(hidden.tick);
    // A 2s hidden interval must not become a burst of 50 overdue simulation ticks.
    expect((await snapshot()).tick-hidden.tick).toBeLessThan(15);
    await page.locator('#canvas').press('Escape',{delay:80});
    // The menu does not set the explicit simulation-pause flag. Wait for the
    // presented Quit label instead of treating frame count as menu readiness.
    await expect.poll(()=>require('../tests/pixels').hasLightText(page,
      {x:460,y:482,width:280,height:34})).toBe(true);
    const frames=(await snapshot()).frames;
    await expect.poll(async ()=>(await snapshot()).frames).toBeGreaterThan(frames+2);
    await click(320,290); await screen('EndGameScreen');
  } catch (error) {
    if (page && !page.isClosed()) {
      await page.screenshot({path:info.outputPath('visibility-failure.png')}).catch(()=>{});
      await info.attach('visibility-state', {body:JSON.stringify(await page.evaluate(()=>glob2Diagnostics.snapshot()).catch(()=>null)),contentType:'application/json'});
      console.log('Visibility input diagnostics:',await page.evaluate(()=>({events:window.visibilityInputs,ratio:devicePixelRatio,inner:[innerWidth,innerHeight]})).catch(()=>null));
    }
    throw error;
  } finally {
    if(browser) await browser.close();
    if(child.exitCode===null) child.kill();
    await exited;
    await fs.rm(profile,{recursive:true,force:true});
  }
});
