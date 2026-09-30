const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL,clickCustomGameStart,clickControl,clickListRow}=require('./main-menu');
const path=require('node:path');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);

async function startAndSave(page) {
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'save');
  // Name the manual save explicitly: an autosave can arrive while persistence
  // completes, so selecting the first newly appearing file races with it.
  await clickControl(page,'name');
  const nameField=page.getByRole('textbox',{name:'Game text field'});
  await nameField.click();
  await nameField.press('Home');
  await page.keyboard.type('Reload regression ',{delay:30});
  await clickControl(page,'ok');
  await expect.poll(async()=>{
    const saves=await page.evaluate(()=>glob2Diagnostics.saves());
    return saves.filter(name=>name.startsWith('Reload_regression_')).length;
  }).toBe(1);
  await expect.poll(async()=>(await state(page)).persistence).toBe('persisted');
  return (await page.evaluate(()=>glob2Diagnostics.saves())).find(name=>name.startsWith('Reload_regression_'));
}
async function loadSaved(page,replay=false,observeLoading=true) {
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'load');
  // Game loads retain the last saved name; the first row may be an autosave.
  // The imported replay is selected explicitly because it has a different name.
  if(replay) await clickListRow(page,'files',/^AAA Replay/);
  // Start observing before the click so a fast cooperative load is not missed.
  const loading=observeLoading ? page.waitForFunction(()=>glob2Diagnostics.snapshot().screenClass.includes('GameLoadScreen')) : Promise.resolve();
  await clickControl(page,'ok');await loading;
}

test('an active match can load the same saved game repeatedly through the scheduled loader',async({page})=>{
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  const name=await startAndSave(page);
  const digest=await page.evaluate(name=>glob2Diagnostics.saveDigest(name),name);
  for(let repeat=0;repeat<2;++repeat) {
    const before=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(before+25);
    await loadSaved(page);await screen(page,'match');
    expect((await state(page)).screenClass).toContain('GameSessionScreen');
    const loaded=(await state(page)).tick;
    await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(loaded+25);
    expect(await page.evaluate(name=>glob2Diagnostics.saveDigest(name),name)).toEqual(digest);
  }
  expect(errors).toEqual([]);
});

test('a damaged in-game load returns through a scheduled error notice and permits another match',{tag:'@webgl2-reload-b'},async({page})=>{
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  const name=await startAndSave(page);
  // Let the initial autosave appear first so loading cannot accidentally select it.
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(85);
  // Damage the stored bytes at the filesystem boundary; all loading uses UI input.
  await page.evaluate(name=>FS.writeFile('/home/web_user/.glob2/games/'+name,new Uint8Array([1,2,3])),name);
  await loadSaved(page,false,false);await screen(page,'MessageScreen');
  await page.setViewportSize({width:800,height:600});
  await expect.poll(async()=>(await state(page)).width).toBe(800);
  await page.locator('#canvas').press('Escape',{delay:80});await screen(page,'CustomGameScreen');
  await page.setViewportSize({width:1200,height:900});
  await expect.poll(async()=>(await state(page)).width).toBe(1200);
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await screen(page,'match');
  const restarted=(await state(page)).tick;
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(restarted+25);
  expect(errors).toEqual([]);
});

test('an active replay can be loaded again through the scheduled loader',{tag:'@webgl2-reload-c'},async({page})=>{
  const loadTimeout=process.env.GLOB2_TEST_RENDERER==='webgl2'?180000:30000;
  if(process.env.GLOB2_TEST_RENDERER==='webgl2') test.setTimeout(420000);
  const errors=[];page.on('pageerror',error=>errors.push(String(error)));
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'load');await screen(page,'ChooseMapScreen');await clickControl(page,'switch');
  const chooser=page.waitForEvent('filechooser');await clickControl(page,'import');
  await (await chooser).setFiles({name:'AAA Replay.replay',mimeType:'application/octet-stream',
    buffer:await require('node:fs/promises').readFile(path.resolve(__dirname,'fixtures/cross-replay.replay'))});
  await expect.poll(async()=>(await state(page)).import).toBe('succeeded');
  await clickListRow(page,'files',/^AAA Replay/);await clickControl(page,'ok');
  await expect.poll(async()=>(await state(page)).screen,{timeout:loadTimeout}).toContain('match');
  const original=(await state(page)).tick;
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(original+25);
  await loadSaved(page,true);
  await expect.poll(async()=>(await state(page)).screen,{timeout:loadTimeout}).toContain('match');
  const loaded=(await state(page)).tick;
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(loaded+25);
  expect(errors).toEqual([]);
});
