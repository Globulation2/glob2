require('../browser-proxy-diagnostics');
const {editTextField}=require('../../../browser/tests/main-menu');
const {gameURL,clickMainMenu,clickSettingsCancel,clickCustomGameStart,clickControl,control}=require('../../../browser/tests/main-menu');
const {test,expect}=require('@playwright/test');
const fs=require('node:fs/promises');
const {createHash}=require('node:crypto');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async ()=>(await state(page)).screen).toContain(name);
async function save(page) {
  await page.locator('#canvas').press('Escape',{delay:80});
  const frames=(await state(page)).frames;
  await expect.poll(async ()=>(await state(page)).frames).toBeGreaterThan(frames+2);
  await clickControl(page,'save');
  await editTextField(page,'Durability regression');
  await clickControl(page,'ok');
}
for (const fault of ['abort','quota']) test(`${fault} failure retains the previous durable save and can retry`,async ({page,context},info)=>{
  // Inject a failure at the browser database boundary, not into game behavior.
  await page.addInitScript(fault=>{
    let fail=false;
    window.storageFault={enable:()=>fail=true,disable:()=>fail=false};
    const transaction=IDBDatabase.prototype.transaction;
    IDBDatabase.prototype.transaction=function(...args){
      const tx=transaction.apply(this,args);
      if(fail && fault==='abort' && args[1]==='readwrite') queueMicrotask(()=>tx.abort());
      return tx;
    };
    const put=IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put=function(...args) {
      if(fail && fault==='quota') throw new DOMException('Injected quota exhaustion','QuotaExceededError');
      return put.apply(this,args);
    };
  }, fault);
  await page.goto(gameURL()); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async ()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('p',{delay:80});
  await expect.poll(async ()=>(await state(page)).paused).toBe(true);
  const digest=()=>page.evaluate(()=>glob2Diagnostics.saveDigest('Durability_regression.game.gz'));
  await save(page);
  await expect.poll(digest).not.toBeNull();
  await expect.poll(async ()=>(await state(page)).persistence).toBe('persisted');
  const original=await digest(), tick=(await state(page)).tick;
  await page.locator('#canvas').press('p',{delay:80});
  await expect.poll(async ()=>(await state(page)).tick).toBeGreaterThan(tick+25);
  await page.locator('#canvas').press('p',{delay:80});
  await expect.poll(async ()=>(await state(page)).paused).toBe(true);
  await page.evaluate(()=>storageFault.enable());
  await save(page);
  // Background writes can fail before the worker consumes the save click.
  // Export appears only after this dialog's persistence operation has failed;
  // the global storage status alone can still describe an earlier write.
  await control(page,'export');
  await expect.poll(async ()=>(await state(page)).persistence).toBe('failed');
  const changed=await digest(); expect(changed).not.toEqual(original);
  const downloadEvent=page.waitForEvent('download');
  await clickControl(page,'export');
  const download=await downloadEvent;
  expect(download.suggestedFilename()).toBe('Durability_regression.game.gz');
  const exported=await fs.readFile(await download.path());
  expect({size:exported.length,sha256:createHash('sha256').update(exported).digest('hex')}).toEqual(changed);
  await page.screenshot({path:info.outputPath('save-persistence-failure.png')});
  const restored=await context.newPage();
  await restored.goto(gameURL()); await screen(restored,'MainMenuScreen');
  expect(await restored.evaluate(()=>glob2Diagnostics.saveDigest('Durability_regression.game.gz'))).toEqual(original);
  await restored.close();
  await page.evaluate(()=>storageFault.disable());
  // The original save dialog is retained; click OK to retry the operation.
  await clickControl(page,'ok');
  await expect.poll(async ()=>(await state(page)).persistence).toBe('persisted');
  // Retry serializes the current game again; compare with that replacement,
  // rather than assuming every local GUI field is unchanged since the failure.
  const retried=await digest(); expect(retried).not.toEqual(original);
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await digest()).toEqual(retried);
});

test('restore failure is explained before entering the game', async ({page},info)=>{
  await page.addInitScript(()=>{
    const open=IDBFactory.prototype.open;
    window.restoreFault={attempts:0};
    IDBFactory.prototype.open=function(...args){
      // Emscripten also opens a database for its asset cache. This fault and
      // retry count belong to the mounted save directory only.
      if(args[0]==='/home/web_user'){
        ++restoreFault.attempts;
        if(!sessionStorage.getItem('restoreFaultDisabled'))
          throw new DOMException('Injected storage refusal','SecurityError');
      }
      return open.apply(this,args);
    };
  });
  await page.goto(gameURL()); await screen(page,'MessageScreen');
  expect((await state(page)).restore).toBe('failed');
  expect((await state(page)).persistence).toBe('restore-failed');
  await page.screenshot({path:info.outputPath('storage-restore-failure.png')});
  await clickControl(page,'choice/0'); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'settings'); await screen(page,'SettingsScreen');
  await clickSettingsCancel(page); await screen(page,'MainMenuScreen');
  // Startup and settings writes must not retry the database after failed restore.
  expect(await page.evaluate(()=>restoreFault.attempts)).toBe(1);
  expect((await state(page)).persistence).toBe('restore-failed');
  await page.evaluate(()=>sessionStorage.setItem('restoreFaultDisabled','1'));
  await page.reload(); await screen(page,'MainMenuScreen');
  expect((await state(page)).restore).toBe('ready');
});

const sanitizerErrors=new WeakMap();
test.beforeEach(async({context},info)=>{
 const errors=[];sanitizerErrors.set(context,errors);
 context.on('page',p=>{
  p.on('pageerror',e=>errors.push(String(e)));
  p.on('console',m=>{if(/AddressSanitizer|ERROR:|SUMMARY:/.test(m.text()))errors.push(m.text());});
 });
 for(const p of context.pages()){
  p.on('pageerror',e=>errors.push(String(e)));
  p.on('console',m=>{if(/AddressSanitizer|ERROR:|SUMMARY:/.test(m.text()))errors.push(m.text());});
 }
});
test.afterEach(async({context},info)=>{
 const errors=sanitizerErrors.get(context)||[];
 console.log('Sanitizer errors',JSON.stringify(errors));
 expect(errors).toEqual([]);
});
