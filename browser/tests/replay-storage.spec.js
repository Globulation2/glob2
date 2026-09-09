const {test,expect}=require('@playwright/test');
const {gameURL}=require('./game-url');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});
const menu=(page,x,y)=>{const {width,height}=page.viewportSize();return click(page,x+(width-640)/2,y+(height-480)/2);};
const dialog=(page,x,y)=>{const {width,height}=page.viewportSize();return click(page,x+Math.floor((width-300)/2),y+Math.floor((height-275)/2));};
const digest=(page,name)=>page.evaluate(async name=>{
  const path='/home/web_user/.glob2/replays/'+name+'.replay';
  if(!FS.analyzePath(path).exists)return null;
  const bytes=FS.readFile(path);
  return {size:bytes.length,hash:Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))).join(',')};
},name);

async function results(page) {
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await menu(page,480,200);await screen(page,'CustomGameScreen');
  await menu(page,100,70);await menu(page,530,380);
  await expect.poll(async()=>(await state(page)).tick,{timeout:60000}).toBeGreaterThan(25);
  await page.locator('#canvas').press('Escape',{delay:80});
  const {width,height}=page.viewportSize();
  await click(page,width/2,height/2+50);await screen(page,'EndGameScreen');
}
async function openSave(page,name) {
  await page.locator('#canvas').press('s',{delay:80});await screen(page,'ReplaySaveScreen');
  await dialog(page,150,202);await page.keyboard.type(name);
}
test.beforeEach(async({page})=>{test.setTimeout(120000);await results(page);});

test('replay save cancellation and pending storage retain the results session',async({page})=>{
  await openSave(page,'Cancelled');
  await page.locator('#canvas').press('Escape',{delay:80});await screen(page,'EndGameScreen');
  expect(await digest(page,'Cancelled')).toBeNull();
  await openSave(page,'Deferred');
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    FS.syncfs=function(populate,callback){
      if(!populate){window.releaseReplayWrite=()=>{FS.syncfs=sync;sync.call(FS,populate,callback);};return;}
      return sync.call(FS,populate,callback);
    };
  });
  await dialog(page,77,245);
  await expect.poll(()=>page.evaluate(()=>typeof window.releaseReplayWrite)).toBe('function');
  await page.setViewportSize({width:1000,height:700});
  await expect.poll(async()=>(await state(page)).width).toBe(1000);
  await page.locator('#canvas').press('Escape',{delay:80});await screen(page,'ReplaySaveScreen');
  await page.evaluate(()=>window.releaseReplayWrite());await screen(page,'EndGameScreen');
  const saved=await digest(page,'Deferred');expect(saved.size).toBeGreaterThan(100);
  await page.reload();await screen(page,'MainMenuScreen');
  expect(await digest(page,'Deferred')).toEqual(saved);
});

for(const fault of ['quota','aborted transaction'])test(`replay save reports ${fault} and retries durably`,async({page},info)=>{
  await openSave(page,'Retry');
  await page.evaluate(fault=>{
    const put=IDBObjectStore.prototype.put;
    window.failReplayWrite=true;
    IDBObjectStore.prototype.put=function(...args){
      if(window.failReplayWrite){
        if(fault==='quota')throw new DOMException('Injected quota exhaustion','QuotaExceededError');
        this.transaction.abort();
      }
      return put.apply(this,args);
    };
  },fault);
  await dialog(page,77,245);
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');
  await screen(page,'ReplaySaveScreen');
  await page.screenshot({path:info.outputPath('replay-save-failure.png')});
  const staged=await digest(page,'Retry');expect(staged.size).toBeGreaterThan(100);
  await page.evaluate(()=>window.failReplayWrite=false);
  await dialog(page,77,245);await screen(page,'EndGameScreen');
  await page.reload();await screen(page,'MainMenuScreen');
  expect(await digest(page,'Retry')).toEqual(staged);
});
