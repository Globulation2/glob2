const {clickCreateMap}=require('./editor-controls');
const {editTextField,clickControl}=require('./main-menu');
const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL}=require('./main-menu');
const fs=require('node:fs/promises');
const {createHash}=require('node:crypto');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);

for(const fault of ['quota','aborted transaction']) test(`editor save before quit survives ${fault} with export and retry`,async({page,context},info)=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'editor');await screen(page,'EditorMainMenu');
  await clickControl(page,'new-map');await screen(page,'NewMapScreen');
  await clickCreateMap(page);await screen(page,'MapEditorScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'quit');await screen(page,'MessageScreen');
  await clickControl(page,'choice/0');await screen(page,'MapEditorScreen'); // Save before quitting.
  await editTextField(page,'Editor durability');
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
  await clickControl(page,'ok');
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await screen(page,'MapEditorScreen');
  const digest=()=>page.evaluate(()=>glob2Diagnostics.mapDigest('Editor_durability.map.gz'));
  const local=await digest();expect(local).not.toBeNull();
  const downloadEvent=page.waitForEvent('download');await clickControl(page,'export');
  const download=await downloadEvent;expect(download.suggestedFilename()).toBe('Editor_durability.map.gz');
  const bytes=await fs.readFile(await download.path());
  expect({size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')}).toEqual(local);
  await page.screenshot({path:info.outputPath('editor-save-failure.png')});
  const restored=await context.newPage();await restored.goto(gameURL());await screen(restored,'MainMenuScreen');
  expect(await restored.evaluate(()=>glob2Diagnostics.mapDigest('Editor_durability.map.gz'))).toBeNull();
  await restored.close();
  await page.evaluate(()=>window.editorStorageFault=false);
  await clickControl(page,'ok');await screen(page,'EditorMainMenu');
  await expect.poll(async()=>(await state(page)).persistence).toBe('persisted');
  const saved=await digest();expect(saved).not.toBeNull();
  await page.reload();await screen(page,'MainMenuScreen');expect(await digest()).toEqual(saved);
});
