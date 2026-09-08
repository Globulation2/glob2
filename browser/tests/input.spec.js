const {test,expect}=require('@playwright/test');
const {gameURL}=require('./game-url');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});

test('click coordinates stay correct when browser motion delivery is missing',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await click(page,760,410);await screen(page,'CustomGameScreen');
  // Inject a lost/coalesced motion delivery, while keeping actual player button
  // input. SDL must receive the button's own position, not its last hover point.
  await page.evaluate(()=>document.addEventListener('mousemove',event=>{
    if(event.isTrusted)event.stopImmediatePropagation();
  },true));
  await click(page,380,280);await click(page,810,590);
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.setViewportSize({width:1000,height:700});
  await expect.poll(async()=>(await state(page)).width).toBe(1000);
  await page.locator('#canvas').press('Escape',{delay:80});
  await expect.poll(()=>require('./pixels').hasLightText(page,{x:360,y:382,width:280,height:34})).toBe(true);
  await click(page,500,400);await screen(page,'EndGameScreen');
});
