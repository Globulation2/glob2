const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL}=require('./main-menu');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});

async function start(page){
  await page.goto(gameURL()); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  await click(page,600,650); await screen(page,'MainMenuScreen');
}
async function failWrites(page){
  await page.evaluate(()=>{
    const put=IDBObjectStore.prototype.put;
    window.failShutdownWrite=true;
    IDBObjectStore.prototype.put=function(...args){
      if(window.failShutdownWrite)throw new DOMException('Injected quota exhaustion','QuotaExceededError');
      return put.apply(this,args);
    };
  });
}

test('quit waits for final persistence through resize and escape',async({page})=>{
  await start(page);
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    FS.syncfs=function(populate,callback){
      if(!populate){window.releaseShutdownWrite=()=>{FS.syncfs=sync;sync.call(FS,populate,callback);};return;}
      return sync.call(FS,populate,callback);
    };
  });
  await clickMainMenu(page,'quit'); await screen(page,'ShutdownScreen');
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseShutdownWrite)).toBe('function');
  await page.setViewportSize({width:1000,height:700});
  await expect.poll(async()=>(await state(page)).width).toBe(1000);
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'ShutdownScreen');
  await page.evaluate(()=>window.releaseShutdownWrite());
  await screen(page,'exited');
  expect((await state(page)).persistence).toBe('persisted');
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await page.evaluate(()=>glob2Diagnostics.preferences())).toEqual({optionFlags:1,mute:1});
});

test('quit offers visible retry after a failed final save',async({page},info)=>{
  await start(page); await failWrites(page);
  await clickMainMenu(page,'quit'); await screen(page,'ShutdownScreen');
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await expect.poll(()=>require('./pixels').hasLightText(page,{x:300,y:440,width:600,height:24})).toBe(true);
  await page.screenshot({path:info.outputPath('shutdown-save-failure.png')});
  await page.evaluate(()=>window.failShutdownWrite=false);
  await click(page,440,570); await screen(page,'exited');
  expect((await state(page)).persistence).toBe('persisted');
});

test('failed final save requires an explicit quit without saving',async({page})=>{
  await start(page); await failWrites(page);
  await clickMainMenu(page,'quit'); await screen(page,'ShutdownScreen');
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await page.locator('#canvas').press('Escape',{delay:80}); await screen(page,'ShutdownScreen');
  await click(page,760,570); await screen(page,'exited');
  expect((await state(page)).persistence).toBe('failed');
});
