const {test,expect}=require('@playwright/test');
const {gameURL}=require('./game-url');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});
const preferences=page=>page.evaluate(()=>glob2Diagnostics.preferences());

test('new browser profiles are muted and an explicit unmute survives reload',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,440,570);await screen(page,'SettingsScreen');
  await click(page,600,650);await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual({optionFlags:1,mute:1});
  await click(page,440,570);await screen(page,'SettingsScreen');
  await click(page,520,585); // Actual Mute checkbox, relative to the centered settings panel.
  await click(page,600,650);await screen(page,'MainMenuScreen');
  expect((await preferences(page)).mute).toBe(0);
  await page.reload();await screen(page,'MainMenuScreen');
  await click(page,440,570);await screen(page,'SettingsScreen');
  await click(page,600,650);await screen(page,'MainMenuScreen');
  expect((await preferences(page)).mute).toBe(0);
});

for (const fault of ['quota','aborted transaction']) test(`settings survive ${fault} with visible failure and durable retry`,async({page,context},info)=>{
  await page.goto(gameURL()); await screen(page,'MainMenuScreen');
  await click(page,440,570); await screen(page,'SettingsScreen');
  await click(page,600,650); await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual({optionFlags:1,mute:1});
  await click(page,440,570); await screen(page,'SettingsScreen');
  await click(page,520,370); // Turn high-quality graphics on using the actual toggle.
  await page.evaluate(fault=>{
    window.settingsStorageFault=true;
    const put=IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put=function(...args){
      if(window.settingsStorageFault){
        if(fault==='quota')throw new DOMException('Injected quota exhaustion','QuotaExceededError');
        this.transaction.abort();
      }
      return put.apply(this,args);
    };
  },fault);
  await click(page,600,650);
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await screen(page,'SettingsScreen');
  await expect.poll(()=>require('./pixels').hasLightText(page,{x:300,y:604,width:600,height:22})).toBe(true);
  await page.screenshot({path:info.outputPath('settings-save-failure.png')});
  const restored=await context.newPage(); await restored.goto(gameURL()); await screen(restored,'MainMenuScreen');
  expect(await preferences(restored)).toEqual({optionFlags:1,mute:1}); await restored.close();
  await page.evaluate(()=>window.settingsStorageFault=false);
  await click(page,600,650); await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual({optionFlags:0,mute:1});
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual({optionFlags:0,mute:1});
});

test('settings wait for durable storage before closing',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,440,570);await screen(page,'SettingsScreen');
  // Stall the storage adapter, without a production test flag or changing game state.
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    FS.syncfs=function(populate,callback){
      if(!populate){ window.releaseSettingsWrite=()=>{FS.syncfs=sync;sync.call(FS,populate,callback);}; return; }
      return sync.call(FS,populate,callback);
    };
  });
  await click(page,600,650);
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseSettingsWrite)).toBe('function');
  await screen(page,'SettingsScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'SettingsScreen');
  await page.evaluate(()=>window.releaseSettingsWrite());
  await screen(page,'MainMenuScreen');
});

test('settings can continue after failure without claiming a durable save',async({page,context})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,440,570);await screen(page,'SettingsScreen');
  await click(page,600,650);await screen(page,'MainMenuScreen');
  await click(page,440,570);await screen(page,'SettingsScreen');
  await click(page,520,370);
  await page.evaluate(()=>{IDBObjectStore.prototype.put=function(){throw new DOMException('Injected quota exhaustion','QuotaExceededError');};});
  await click(page,600,650);
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await screen(page,'SettingsScreen');
  await click(page,810,650);await screen(page,'MainMenuScreen');
  expect((await state(page)).persistence).toBe('failed');
  const restored=await context.newPage();await restored.goto(gameURL());await screen(restored,'MainMenuScreen');
  expect(await preferences(restored)).toEqual({optionFlags:1,mute:1});
});
