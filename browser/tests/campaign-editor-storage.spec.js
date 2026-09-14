const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL}=require('./main-menu');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});
const digest=page=>page.evaluate(()=>glob2Diagnostics.campaignDefinitionDigest('Browser_Campaign.txt'));
async function edit(page){
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'editor');await screen(page,'EditorMainMenu');
  await click(page,600,420);await screen(page,'CampaignEditor');
  await click(page,750,280);
  await page.locator('#canvas').press('Home');
  for(let i=0;i<7;i++)await page.locator('#canvas').press('Delete');
  await page.keyboard.type('Browser Campaign',{delay:30});
}

test('campaign authoring waits for durable persistence before closing',async({page})=>{
  await edit(page);
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    FS.syncfs=function(populate,callback){
      if(!populate){window.releaseCampaignWrite=()=>{FS.syncfs=sync;sync.call(FS,populate,callback);};return;}
      return sync.call(FS,populate,callback);
    };
  });
  await click(page,630,660);
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseCampaignWrite)).toBe('function');
  await screen(page,'CampaignEditor');
  await click(page,820,660); // Cancel cannot complete a pending save.
  await screen(page,'CampaignEditor');
  await page.evaluate(()=>window.releaseCampaignWrite());
  await screen(page,'EditorMainMenu');
  const saved=await digest(page);expect(saved?.size).toBeGreaterThan(0);
  await page.reload();await screen(page,'MainMenuScreen');
  expect(await digest(page)).toEqual(saved);
});

test('campaign authoring retains the editor on quota failure and can retry',async({page,context},info)=>{
  await edit(page);
  await page.evaluate(()=>{
    const put=IDBObjectStore.prototype.put;
    window.failCampaignWrite=true;
    IDBObjectStore.prototype.put=function(...args){
      if(window.failCampaignWrite)throw new DOMException('Injected quota exhaustion','QuotaExceededError');
      return put.apply(this,args);
    };
  });
  await click(page,630,660);
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await screen(page,'CampaignEditor');
  await expect.poll(()=>require('./pixels').hasLightText(page,{x:610,y:548,width:290,height:65})).toBe(true);
  await page.screenshot({path:info.outputPath('campaign-editor-save-failure.png')});
  const restored=await context.newPage();await restored.goto(gameURL());await screen(restored,'MainMenuScreen');
  expect(await digest(restored)).toBeNull();await restored.close();
  await page.evaluate(()=>window.failCampaignWrite=false);
  await click(page,630,660);await screen(page,'EditorMainMenu');
  const saved=await digest(page);expect(saved?.size).toBeGreaterThan(0);
  await page.reload();await screen(page,'MainMenuScreen');
  expect(await digest(page)).toEqual(saved);
});
