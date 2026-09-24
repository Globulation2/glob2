const {test,expect}=require('@playwright/test');
const {gameURL,clickMainMenu,clickCustomGameStart}=require('./main-menu');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
async function colonyVisible(page) {
  const png=await page.screenshot({clip:{x:0,y:50,width:24,height:200}});
  return page.evaluate(async base64=>{
    const bitmap=await createImageBitmap(await (await fetch('data:image/png;base64,'+base64)).blob());
    const c=document.createElement('canvas');c.width=bitmap.width;c.height=bitmap.height;
    const ctx=c.getContext('2d');ctx.drawImage(bitmap,0,0);bitmap.close();
    const data=ctx.getImageData(0,0,c.width,c.height).data;
    let water=0;for(let i=0;i<data.length;i+=4)if(data[i+2]>data[i+1]*1.25 && data[i+2]>data[i]*1.1)++water;
    return water/(data.length/4)>0.25;
  },png.toString('base64'));
}
test('custom setup leaves the colony visible around its own panel',async({page},info)=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
  await expect.poll(()=>colonyVisible(page)).toBe(true);
  await page.screenshot({path:info.outputPath('custom-colony.png')});
});
test('menu presentation survives a match and nested menus',async({page},info)=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await page.screenshot({path:info.outputPath('main.png')});
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
  await page.screenshot({path:info.outputPath('custom.png')});
  await clickCustomGameStart(page);
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('Escape',{delay:80});
  await page.locator('#canvas').click({position:{x:600,y:500},delay:80});
  await screen(page,'EndGameScreen');
  await page.screenshot({path:info.outputPath('results.png')});
  await page.locator('#canvas').click({position:{x:1060,y:815},delay:80});
  await screen(page,'LoadSaveScreen');
  await expect.poll(()=>require('./pixels').hasDarkText(page,{x:475,y:317,width:250,height:30})).toBe(true);
  await page.screenshot({path:info.outputPath('save-replay.png')});
  await page.locator('#canvas').press('Escape',{delay:80});await screen(page,'EndGameScreen');
  await page.locator('#canvas').press('Enter',{delay:80});await screen(page,'CustomGameScreen');
  await page.screenshot({path:info.outputPath('custom-return.png')});
  await expect.poll(()=>colonyVisible(page)).toBe(true);
  await page.locator('#canvas').press('Escape',{delay:80});await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'campaign');await screen(page,'CampaignMainMenu');
  await page.screenshot({path:info.outputPath('campaign-after-game.png')});
  await expect.poll(()=>colonyVisible(page)).toBe(true);
});

for(const deviceScaleFactor of [1,2]) test.describe(`cursor at pixel ratio ${deviceScaleFactor}`,()=>{
  test.use({deviceScaleFactor});
  test('browser keeps its system cursor through menus, gameplay and reload',async({page})=>{
  const cursor=()=>page.locator('#canvas').evaluate(c=>getComputedStyle(c).cursor);
  const normal=async()=>expect(await cursor()).toMatch(/^(auto|default)$/);
  await page.goto(gameURL());await screen(page,'MainMenuScreen');await normal();
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');await normal();
  await clickCustomGameStart(page);await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.mouse.move(1075,205);await normal();
  await page.mouse.move(400,350);await normal();
  await page.reload();await screen(page,'MainMenuScreen');await normal();
});
});
