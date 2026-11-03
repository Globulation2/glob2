const {test, expect}=require('@playwright/test');
const {waitForMainMenu,clickMainMenu,gameURL}=require('../tests/main-menu');

// Throttle actual host animation callbacks in the page and application worker.
// Asset processing must continue yielding on timer turns between display frames.
const slowAnimation=`(() => {
  if(globalThis.__glob2SlowStartupFrames)return;
  globalThis.__glob2SlowStartupFrames=true;
  Object.defineProperty(navigator,'hardwareConcurrency',{configurable:true,get:()=>2});
  const frame=globalThis.requestAnimationFrame?.bind(globalThis);
  const cancel=globalThis.cancelAnimationFrame?.bind(globalThis);
  if(!frame)return;
  let next=-1;const pending=new Map();
  globalThis.requestAnimationFrame=callback=>{
    const id=next--,entry={};pending.set(id,entry);
    entry.timer=setTimeout(()=>{entry.frame=frame(time=>{pending.delete(id);callback(time);});},1000);
    return id;
  };
  globalThis.cancelAnimationFrame=id=>{
    const entry=pending.get(id);
    if(!entry){cancel?.(id);return;}
    clearTimeout(entry.timer);if(entry.frame!==undefined)cancel?.(entry.frame);pending.delete(id);
  };
})();`;
for(const mode of ['serial','threaded']){
  test(`${mode} asset startup advances between infrequent display callbacks`,async({page})=>{
    await page.addInitScript(slowAnimation);
    await page.context().route('**/index.js',async route=>{
      const response=await route.fetch();
      await route.fulfill({response,body:slowAnimation+'\n'+await response.text()});
    });
    const url=new URL(gameURL(),'http://localhost');url.searchParams.set('threads',mode);
    await page.goto(url.pathname+url.search);
    await waitForMainMenu(page);
    const state=()=>page.evaluate(()=>glob2Diagnostics.snapshot());
    expect((await state()).executionMode).toBe(mode);
    expect((await state()).artworkReady).toBe(true);
    await clickMainMenu(page,'settings');
    await expect.poll(async()=>(await state()).screen).toContain('SettingsScreen');
  });
}
