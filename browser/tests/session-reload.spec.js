const {resizeAndWait}=require('./viewport-ready');
const {test,expect}=require('@playwright/test');
const path=require('node:path');
const {gameURL}=require('./game-url');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});

async function startAndSave(page) {
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,760,410);await screen(page,'CustomGameScreen');
  await click(page,380,280);await click(page,810,590);
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,600,400);await click(page,520,555);
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.saves().length)).toBe(1);
  await expect.poll(async()=>(await state(page)).persistence).toBe('persisted');
  return (await page.evaluate(()=>glob2Diagnostics.saves()))[0];
}
async function loadFirst(page,replay=false,observeLoading=true) {
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,600,replay?375:350);
  await click(page,500,360);
  // Start observing before the click so a fast cooperative load is not missed.
  const loading=observeLoading ? page.waitForFunction(()=>glob2Diagnostics.snapshot().screenClass.includes('GameLoadScreen')) : Promise.resolve();
  await click(page,520,555);await loading;
}

test('an active match can load the same saved game repeatedly through the scheduled loader',async({page})=>{
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  const name=await startAndSave(page);
  const digest=await page.evaluate(name=>glob2Diagnostics.saveDigest(name),name);
  for(let repeat=0;repeat<2;++repeat) {
    const before=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(before+25);
    await loadFirst(page);await screen(page,'match');
    expect((await state(page)).screenClass).toContain('GameSessionScreen');
    const loaded=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(loaded+25);
    expect(await page.evaluate(name=>glob2Diagnostics.saveDigest(name),name)).toEqual(digest);
  }
  expect(errors).toEqual([]);
});

test('a damaged in-game load returns through a scheduled error notice and permits another match',async({page})=>{
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  const name=await startAndSave(page);
  // Damage the stored bytes at the filesystem boundary; all loading uses UI input.
  await page.evaluate(name=>FS.writeFile('/home/web_user/.glob2/games/'+name,new Uint8Array([1,2,3])),name);
  await loadFirst(page,false,false);await screen(page,'MessageScreen');
  await resizeAndWait(page, {width:800,height:600});
  await expect.poll(async()=>(await state(page)).width).toBe(800);
  await page.locator('#canvas').press('Escape',{delay:80});await screen(page,'CustomGameScreen');
  await resizeAndWait(page, {width:1200,height:900});
  await expect.poll(async()=>(await state(page)).width).toBe(1200);
  await click(page,380,280);await click(page,810,590);
  await screen(page,'match');
  const restarted=(await state(page)).tick;
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(restarted+25);
  expect(errors).toEqual([]);
});

test('an active replay can be loaded again through the scheduled loader',async({page})=>{
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,440,410);await screen(page,'ChooseMapScreen');await click(page,620,650);
  const chooser=page.waitForEvent('filechooser');await click(page,340,695);
  await (await chooser).setFiles({name:'AAA Replay.replay',mimeType:'application/octet-stream',
    buffer:await require('node:fs/promises').readFile(path.resolve(__dirname,'../../tests/baselines/cross-replay.replay'))});
  await expect.poll(async()=>(await state(page)).import).toBe('succeeded');
  await click(page,380,280);await click(page,810,590);await screen(page,'match');
  const original=(await state(page)).tick;
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(original+25);
  await loadFirst(page,true);await screen(page,'match');
  const loaded=(await state(page)).tick;
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(loaded+25);
  expect(errors).toEqual([]);
});
