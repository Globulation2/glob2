const {test,expect}=require('@playwright/test');
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
  await page.goto('/'); await screen(page,'MainMenuScreen');
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
  await page.screenshot({path:info.outputPath('save-persistence-failure.png')});
  const restored=await context.newPage();
  await restored.goto('/'); await screen(restored,'MainMenuScreen');
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
