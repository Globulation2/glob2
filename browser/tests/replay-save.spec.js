const {resizeAndWait}=require('./viewport-ready');
const {test,expect}=require('@playwright/test');
const {gameURL}=require('./game-url');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});
const digest=page=>page.evaluate(()=>glob2Diagnostics.replayDigest('AAA_review_replay.replay'));

async function endMatch(page) {
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,760,410);await screen(page,'CustomGameScreen');
  await click(page,380,280);await click(page,810,590);
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(50);
  await page.locator('#canvas').press('Escape');await click(page,600,500);
  await screen(page,'EndGameScreen');
  await click(page,1060,815);await screen(page,'ReplaySaveScreen');
  await click(page,600,515);await page.locator('#canvas').pressSequentially('AAA review replay',{delay:20});
}

test('end-game replay save remains scheduled during resize and durable persistence',async({page})=>{
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await endMatch(page);
  await resizeAndWait(page, {width:800,height:600});
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    FS.syncfs=function(populate,callback){
      if(!populate){window.releaseReplaySave=()=>{FS.syncfs=sync;sync.call(FS,populate,callback);};return;}
      return sync.call(FS,populate,callback);
    };
  });
  await click(page,320,405);
  await expect.poll(async()=>(await state(page)).persistence).toBe('writing');
  await screen(page,'ReplaySaveScreen');
  await page.locator('#canvas').press('Escape');await screen(page,'ReplaySaveScreen');
  await page.evaluate(()=>releaseReplaySave());await screen(page,'EndGameScreen');
  const saved=await digest(page);expect(saved).not.toBeNull();
  await page.reload();await screen(page,'MainMenuScreen');expect(await digest(page)).toEqual(saved);
  // Load the file through the real replay chooser after a fresh browser startup.
  await resizeAndWait(page, {width:1200,height:900});
  await click(page,440,410);await screen(page,'ChooseMapScreen');
  await click(page,620,650); // Switch the chooser from saved games to replays.
  await click(page,370,290);await click(page,810,590);
  await screen(page,'match');
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  expect(errors).toEqual([]);
});

test('end-game replay save offers export and retry after quota failure',async({page})=>{
  await endMatch(page);
  await page.evaluate(()=>{
    window.replayQuota=true;
    const put=IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put=function(...args){
      if(window.replayQuota)throw new DOMException('Injected quota exhaustion','QuotaExceededError');
      return put.apply(this,args);
    };
  });
  await click(page,520,555);
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');await screen(page,'ReplaySaveScreen');
  const download=page.waitForEvent('download');await click(page,600,420);
  expect((await download).suggestedFilename()).toBe('AAA_review_replay.replay');
  await page.evaluate(()=>window.replayQuota=false);
  await click(page,520,555);await screen(page,'EndGameScreen');
  const saved=await digest(page);await page.reload();await screen(page,'MainMenuScreen');
  expect(await digest(page)).toEqual(saved);
});
