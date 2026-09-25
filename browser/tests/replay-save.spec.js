const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL,clickCustomGameStart}=require('./main-menu');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});
const digest=page=>page.evaluate(()=>glob2Diagnostics.replayDigest('AAA_review_replay.replay'));

async function endMatch(page) {
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(50);
  await page.locator('#canvas').press('Escape');await click(page,600,500);
  await screen(page,'EndGameScreen');
  await click(page,1060,815);await screen(page,'LoadSaveScreen');
  await page.locator('input[aria-label="Game text field"]').fill('AAA review replay');
}

test('end-game replay save remains scheduled during resize and durable persistence',async({page},info)=>{
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await endMatch(page);
  await page.setViewportSize({width:800,height:600});
  await page.evaluate(()=>{
    const sync=FS.syncfs;
    FS.syncfs=function(populate,callback){
      if(!populate){window.releaseReplaySave=()=>{FS.syncfs=sync;sync.call(FS,populate,callback);};return;}
      return sync.call(FS,populate,callback);
    };
  });
  await click(page,320,405);
  await expect.poll(async()=>(await state(page)).persistence).toBe('writing');
  await screen(page,'LoadSaveScreen');
  await page.locator('#canvas').press('Escape');await screen(page,'LoadSaveScreen');
  await page.evaluate(()=>releaseReplaySave());await screen(page,'EndGameScreen');
  const saved=await digest(page);expect(saved).not.toBeNull();
  for (const name of ['AAA_review_replay.replay']) {
    const bytes = await page.evaluate(name => Array.from(FS.readFile('/home/web_user/.glob2/replays/' + name)), name);
    const file = info.outputPath(name);
    require('node:fs').writeFileSync(file, Buffer.from(bytes));
    await info.attach(name, {path:file, contentType:'application/octet-stream'});
  }
  await page.reload();await screen(page,'MainMenuScreen');expect(await digest(page)).toEqual(saved);
  // Load the file through the real replay chooser after a fresh browser startup.
  await page.setViewportSize({width:1200,height:900});
  await page.reload();await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'load');await screen(page,'ChooseMapScreen');
  await click(page,620,650); // Switch the chooser from saved games to replays.
  // The first row is the named export; the next is the unfinished live recording.
  await click(page,370,280);await click(page,810,590);
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
  await expect.poll(async()=>(await state(page)).persistence).toBe('failed');await screen(page,'LoadSaveScreen');
  const download=page.waitForEvent('download');await click(page,600,420);
  expect((await download).suggestedFilename()).toBe('AAA_review_replay.replay');
  await page.evaluate(()=>window.replayQuota=false);
  await click(page,520,555);await screen(page,'EndGameScreen');
  const saved=await digest(page);await page.reload();await screen(page,'MainMenuScreen');
  expect(await digest(page)).toEqual(saved);
});
