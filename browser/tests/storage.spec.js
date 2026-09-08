const {gameURL} = require('./game-url');
const {test,expect}=require('@playwright/test');
const fs=require('node:fs/promises');
const {createHash}=require('node:crypto');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async ()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});
async function save(page) {
  await page.locator('#canvas').press('Escape',{delay:80});
  const frames=(await state(page)).frames;
  await expect.poll(async ()=>(await state(page)).frames).toBeGreaterThan(frames+2);
  await click(page,600,400); await click(page,600,515);
  await page.locator('#canvas').press('Home');
  for(let i=0;i<50;i++) await page.locator('#canvas').press('Delete');
  await page.locator('#canvas').pressSequentially('Durability regression');
  await click(page,520,555);
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
  await click(page,760,410); await screen(page,'CustomGameScreen');
  await click(page,380,280); await click(page,810,590);
  await expect.poll(async ()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('p',{delay:80});
  await expect.poll(async ()=>(await state(page)).paused).toBe(true);
  const digest=()=>page.evaluate(()=>glob2Diagnostics.saveDigest('Durability_regression.game'));
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
  await expect.poll(async ()=>(await state(page)).persistence).toBe('failed');
  const changed=await digest(); expect(changed).not.toEqual(original);
  const downloadEvent=page.waitForEvent('download');
  await click(page,600,422);
  const download=await downloadEvent;
  expect(download.suggestedFilename()).toBe('Durability_regression.game');
  const exported=await fs.readFile(await download.path());
  expect({size:exported.length,sha256:createHash('sha256').update(exported).digest('hex')}).toEqual(changed);
  await page.screenshot({path:info.outputPath('save-persistence-failure.png')});
  const restored=await context.newPage();
  await restored.goto(gameURL()); await screen(restored,'MainMenuScreen');
  expect(await restored.evaluate(()=>glob2Diagnostics.saveDigest('Durability_regression.game'))).toEqual(original);
  await restored.close();
  await page.evaluate(()=>storageFault.disable());
  // The original save dialog is retained; click OK to retry the operation.
  await click(page,520,555);
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
      ++restoreFault.attempts;
      if(!sessionStorage.getItem('restoreFaultDisabled'))
        throw new DOMException('Injected storage refusal','SecurityError');
      return open.apply(this,args);
    };
  });
  await page.goto(gameURL()); await screen(page,'MessageScreen');
  expect((await state(page)).restore).toBe('failed');
  expect((await state(page)).persistence).toBe('restore-failed');
  await page.screenshot({path:info.outputPath('storage-restore-failure.png')});
  await click(page,390,570); await screen(page,'MainMenuScreen');
  await click(page,440,570); await screen(page,'SettingsScreen');
  await click(page,810,650); await screen(page,'MainMenuScreen');
  // Startup and settings writes must not retry the database after failed restore.
  expect(await page.evaluate(()=>restoreFault.attempts)).toBe(1);
  expect((await state(page)).persistence).toBe('restore-failed');
  await page.evaluate(()=>sessionStorage.setItem('restoreFaultDisabled','1'));
  await page.reload(); await screen(page,'MainMenuScreen');
  expect((await state(page)).restore).toBe('ready');
});
