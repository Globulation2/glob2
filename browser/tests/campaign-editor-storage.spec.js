const {campaignFrame,clickCampaignFooter}=require('./editor-controls');
const {editTextField,clickControl,controlBox}=require('./main-menu');
const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL}=require('./main-menu');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const digest=page=>page.evaluate(()=>glob2Diagnostics.campaignDefinitionDigest('Browser_Campaign.txt'));
async function edit(page){
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'editor');await screen(page,'EditorMainMenu');
  await clickControl(page,'new-campaign');await screen(page,'CampaignEditor');
  await editTextField(page,'Browser Campaign');
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
  await clickCampaignFooter(page,true);
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseCampaignWrite)).toBe('function');
  await screen(page,'CampaignEditor');
  // Cancel is disabled while the save is pending; clicking it changes nothing.
  await clickCampaignFooter(page,false,{enabled:false});
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
  await clickCampaignFooter(page,true);
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await screen(page,'CampaignEditor');
  // The failure notice is the first line of the editor panel, above the map list.
  const frame=await campaignFrame(page), maps=await controlBox(page,'maps');
  await expect.poll(()=>require('./pixels').hasDarkText(page,{x:frame.x,y:frame.y,width:frame.width,height:Math.max(24,maps.y-frame.y)})).toBe(true);
  await page.screenshot({path:info.outputPath('campaign-editor-save-failure.png')});
  const restored=await context.newPage();await restored.goto(gameURL());await screen(restored,'MainMenuScreen');
  expect(await digest(restored)).toBeNull();await restored.close();
  await page.evaluate(()=>window.failCampaignWrite=false);
  await clickCampaignFooter(page,true);await screen(page,'EditorMainMenu');
  const saved=await digest(page);expect(saved?.size).toBeGreaterThan(0);
  await page.reload();await screen(page,'MainMenuScreen');
  expect(await digest(page)).toEqual(saved);
});
