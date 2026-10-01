const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL,clickSettingsDone,clickSettingsCancel,clickControl,controlBox}=require('./main-menu');
const {hasDarkText}=require('./pixels');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const {preferences,lowPreferences,cloudPreferences}=require('./settings-preferences');
// Settings controls carry the setting ids; the Display & graphics category is
// selected by default when Settings opens.
const AUDIO_TAB=(page)=>clickControl(page,'nav.1'); // Sidebar "Audio" entry.
const MUTE_ROW=(page)=>clickControl(page,'audio.mute');
const toggleClouds=(page)=>clickControl(page,'graphics.clouds');

test('new browser profiles are muted and an explicit unmute survives reload',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  await clickSettingsDone(page);await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual(lowPreferences);
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  await AUDIO_TAB(page);
  await MUTE_ROW(page); // Actual Mute toggle, in the Audio category.
  await clickSettingsDone(page);await screen(page,'MainMenuScreen');
  expect((await preferences(page)).mute).toBe(0);
  await page.reload();await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  await clickSettingsDone(page);await screen(page,'MainMenuScreen');
  expect((await preferences(page)).mute).toBe(0);
});

for (const fault of ['quota','aborted transaction']) test(`settings survive ${fault} with visible failure and durable retry`,async({page,context},info)=>{
  await page.goto(gameURL()); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  await clickSettingsDone(page); await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual(lowPreferences);
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  // Inject the fault before the change: every edit auto-saves immediately
  // (that's the whole point of the redesigned screen), so injecting after
  // the click would let this write land before the fault ever applies.
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
  await toggleClouds(page); // Change one effect independently of the other seven.
  await expect.poll(async()=>(await state(page)).persistence,{timeout:10000}).toBe('failed');
  await clickSettingsDone(page); // Retries the write; still fails while the fault is active.
  await screen(page,'SettingsScreen');
  // The status line sits at the left of the footer, on the Done button's row.
  const done=await controlBox(page,'done'), panel=await require('./main-menu').rootBox(page,'done');
  await expect.poll(()=>hasDarkText(page,{x:panel.x,y:done.y,width:Math.max(24,done.x-panel.x-160),height:done.height})).toBe(true);
  await page.screenshot({path:info.outputPath('settings-save-failure.png')});
  const restored=await context.newPage(); await restored.goto(gameURL()); await screen(restored,'MainMenuScreen');
  expect(await preferences(restored)).toEqual(lowPreferences); await restored.close();
  await page.evaluate(()=>window.settingsStorageFault=false);
  await clickSettingsDone(page); await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual(cloudPreferences);
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await preferences(page)).toEqual(cloudPreferences);
});

test('settings wait for durable storage before closing',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  // Stall the storage adapter, without a production test flag or changing game state.
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    FS.syncfs=function(populate,callback){
      if(!populate){ window.releaseSettingsWrite=()=>{FS.syncfs=sync;sync.call(FS,populate,callback);}; return; }
      return sync.call(FS,populate,callback);
    };
  });
  await clickSettingsDone(page);
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseSettingsWrite)).toBe('function');
  await screen(page,'SettingsScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'SettingsScreen');
  await page.evaluate(()=>window.releaseSettingsWrite());
  await screen(page,'MainMenuScreen');
});

test('settings can continue after failure without claiming a durable save',async({page,context})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  await clickSettingsDone(page);await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  await page.evaluate(()=>{IDBObjectStore.prototype.put=function(){throw new DOMException('Injected quota exhaustion','QuotaExceededError');};});
  await toggleClouds(page);
  await expect.poll(async()=>(await state(page)).persistence,{timeout:10000}).toBe('failed');
  await screen(page,'SettingsScreen');
  await clickSettingsCancel(page);await screen(page,'MainMenuScreen');
  expect((await state(page)).persistence).toBe('failed');
  const restored=await context.newPage();await restored.goto(gameURL());await screen(restored,'MainMenuScreen');
  expect(await preferences(restored)).toEqual(lowPreferences);
});

for (const failLatest of [false,true]) test(`Done waits for the latest settings write (${failLatest?'failure':'success'})`,async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  await clickSettingsDone(page);await screen(page,'MainMenuScreen');
  await expect.poll(async()=>(await state(page)).persistence).toBe('persisted');
  await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
  await AUDIO_TAB(page);
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    let writes=0;
    FS.syncfs=function(populate,callback){
      if(populate)return sync.call(FS,populate,callback);
      if(++writes===1){
        // The first value is durable, but its completion has not reached C++.
        sync.call(FS,false,error=>{window.releaseFirstSettingsWrite=()=>callback(error);});
      }else{
        window.releaseLatestSettingsWrite=fail=>{
          FS.syncfs=sync;
          if(fail)callback(new Error('Injected latest settings write failure'));
          else sync.call(FS,false,callback);
        };
      }
    };
  });
  await MUTE_ROW(page);
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseFirstSettingsWrite)).toBe('function');
  await MUTE_ROW(page);
  await clickSettingsDone(page);
  await page.evaluate(()=>window.releaseFirstSettingsWrite());
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseLatestSettingsWrite)).toBe('function');
  // Give the screen several frames to consume the stale acknowledgement.
  await page.waitForTimeout(300);
  expect((await state(page)).screen).toContain('SettingsScreen');
  await page.evaluate(fail=>window.releaseLatestSettingsWrite(fail),failLatest);
  if(failLatest){
    await page.waitForTimeout(300);
    expect((await state(page)).screen).toContain('SettingsScreen');
    await clickSettingsDone(page);
  }
  await screen(page,'MainMenuScreen');
  await page.reload();await screen(page,'MainMenuScreen');
  expect((await preferences(page)).mute).toBe(1);
});
