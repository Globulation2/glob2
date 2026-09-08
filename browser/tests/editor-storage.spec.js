const {test,expect}=require('@playwright/test');
const {gameURL}=require('./game-url');
const fs=require('node:fs/promises');
const {createHash}=require('node:crypto');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});

for(const fault of ['quota','aborted transaction']) test(`editor save before quit survives ${fault} with export and retry`,async({page,context},info)=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,760,570);await screen(page,'EditorMainMenu');
  await click(page,600,300);await screen(page,'NewMapScreen');
  await click(page,440,650);await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,600,525);await screen(page,'MessageScreen');
  await click(page,390,570);await screen(page,'MapEditorScreen');
  await click(page,600,515);await page.locator('#canvas').press('Home');
  for(let i=0;i<40;i++)await page.locator('#canvas').press('Delete');
  await page.locator('#canvas').pressSequentially('Editor durability',{delay:20});
  await page.evaluate(fault=>{
    window.editorStorageFault=true;
    const put=IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put=function(...args){
      if(window.editorStorageFault){
        if(fault==='quota')throw new DOMException('Injected quota exhaustion','QuotaExceededError');
        this.transaction.abort();
      }
      return put.apply(this,args);
    };
  },fault);
  await click(page,520,555);
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await screen(page,'MapEditorScreen');
  const digest=()=>page.evaluate(()=>glob2Diagnostics.mapDigest('Editor_durability.map'));
  const local=await digest();expect(local).not.toBeNull();
  const downloadEvent=page.waitForEvent('download');await click(page,600,422);
  const download=await downloadEvent;expect(download.suggestedFilename()).toBe('Editor_durability.map');
  const bytes=await fs.readFile(await download.path());
  expect({size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')}).toEqual(local);
  await page.screenshot({path:info.outputPath('editor-save-failure.png')});
  const restored=await context.newPage();await restored.goto(gameURL());await screen(restored,'MainMenuScreen');
  expect(await restored.evaluate(()=>glob2Diagnostics.mapDigest('Editor_durability.map'))).toBeNull();
  await restored.close();
  await page.evaluate(()=>window.editorStorageFault=false);
  await click(page,520,555);await screen(page,'EditorMainMenu');
  await expect.poll(async()=>(await state(page)).persistence).toBe('persisted');
  const saved=await digest();expect(saved).not.toBeNull();
  await page.reload();await screen(page,'MainMenuScreen');expect(await digest()).toEqual(saved);
});
