const {test,expect}=require('@playwright/test');
const {gameURL}=require('./game-url');
const fs=require('node:fs/promises');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const menu=(page,x,y)=>page.locator('#canvas').click({position:{x:x+280,y:y+210},delay:80});
const stored=page=>page.evaluate(()=>glob2Diagnostics.campaignDigest('Tutorial_Campaign.txt'));
async function tutorial(page) {await menu(page,480,120);await screen(page,'CampaignMenuScreen');}
async function backup(page) {
  const downloading=page.waitForEvent('download');await menu(page,230,495);
  const download=await downloading;expect(download.suggestedFilename()).toBe('campaign-progress.campaign');
  return fs.readFile(await download.path());
}
async function restore(page,buffer) {
  const selecting=page.waitForEvent('filechooser');await menu(page,80,495);
  await (await selecting).setFiles({name:'backup.campaign',mimeType:'application/octet-stream',buffer});
}
// Produce a prior-progress fixture from an actual exported campaign definition.
// Tests deliver it through the user's file chooser, never by editing game state.
function completedFirst(original) {
  const bytes=Buffer.from(original);let offset=8;
  const text=()=>{const length=bytes.readUInt32BE(offset);offset+=4+length;};
  expect(bytes.subarray(0,4).toString()).toBe('G2CP');
  text();text();const count=bytes.readUInt32BE(offset);offset+=4;expect(count).toBeGreaterThan(0);
  text();text();const prerequisites=bytes.readUInt32BE(offset);offset+=4;
  for(let i=0;i<prerequisites;i++)text();
  bytes[offset]=1;bytes[offset+1]=1;
  return bytes;
}

test.beforeEach(async({page})=>{await page.goto(gameURL());await screen(page,'MainMenuScreen');});

test('campaign progress merges through the chooser, survives reload and rejects invalid backups',async({page},info)=>{
  await tutorial(page);
  const initial=await backup(page), completed=completedFirst(initial);
  await restore(page,completed);await expect.poll(async()=>(await state(page)).import).toBe('succeeded');
  expect(await backup(page)).toEqual(completed);
  await page.screenshot({path:info.outputPath('campaign-progress-import.png')});
  await page.reload();await screen(page,'MainMenuScreen');await tutorial(page);
  expect(await backup(page)).toEqual(completed);
  const future=Buffer.from(completed);future.writeUInt32BE(2,4);
  const mismatched=Buffer.from(completed);mismatched[12]^=1; // campaign identity
  for(const invalid of [completed.subarray(0,3),completed.subarray(0,completed.length-1),future,mismatched]) {
    await restore(page,invalid);await expect.poll(async()=>(await state(page)).import).toBe('invalid');
    expect(await backup(page)).toEqual(completed);
  }
  await restore(page,initial);await expect.poll(async()=>(await state(page)).import).toBe('succeeded');
  expect(await backup(page)).toEqual(completed); // importing an older backup cannot erase progress
});

for(const recovery of ['retry','discard']) test(`campaign persistence failure preserves the durable backup with ${recovery}`,async({page,context},info)=>{
  await tutorial(page);const initial=await backup(page);
  await menu(page,480,450);await screen(page,'MainMenuScreen');
  const original=await stored(page);expect(original).not.toBeNull();
  await tutorial(page);
  await page.evaluate(()=>{
    window.campaignQuota=true;
    const put=IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put=function(...args){
      if(window.campaignQuota)throw new DOMException('Injected quota exhaustion','QuotaExceededError');
      return put.apply(this,args);
    };
  });
  const completed=completedFirst(initial);
  await restore(page,completed);await expect.poll(async()=>(await state(page)).import).toBe('failed');
  await screen(page,'CampaignMenuScreen');
  expect(await backup(page)).toEqual(completed);
  await page.screenshot({path:info.outputPath('campaign-persistence-failure.png')});
  const recovered=await context.newPage();await recovered.goto(gameURL());await screen(recovered,'MainMenuScreen');
  expect(await stored(recovered)).toEqual(original);await recovered.close();
  await page.evaluate(()=>window.campaignQuota=false);
  if(recovery==='retry') {
    await menu(page,400,200);await expect.poll(async()=>(await state(page)).import).toBe('succeeded');
  } else {
    await menu(page,480,450);await screen(page,'MainMenuScreen');
    await expect.poll(async()=>(await state(page)).persistence).toBe('persisted');
  }
  await page.reload();await screen(page,'MainMenuScreen');await tutorial(page);
  expect(await backup(page)).toEqual(recovery==='retry'?completed:initial);
});

test('leaving a new campaign waits for persistence and discard removes its uncommitted file',async({page})=>{
  await tutorial(page);
  await page.evaluate(()=>{
    window.campaignQuota=true;
    const put=IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put=function(...args){
      if(window.campaignQuota)throw new DOMException('Injected quota exhaustion','QuotaExceededError');
      return put.apply(this,args);
    };
  });
  await menu(page,480,450);
  await expect.poll(async()=>(await state(page)).import).toBe('failed');
  await screen(page,'CampaignMenuScreen');
  expect(await stored(page)).not.toBeNull();
  await page.evaluate(()=>window.campaignQuota=false);
  await menu(page,480,450);await screen(page,'MainMenuScreen');
  await expect.poll(async()=>(await state(page)).persistence).toBe('persisted');
  await page.reload();await screen(page,'MainMenuScreen');
  expect(await stored(page)).toBeNull();
});
