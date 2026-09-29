const {test,expect}=require('@playwright/test');
const {clickMainMenu,gameURL,clickCustomGameStart}=require('./main-menu');
const state=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await state(page)).screen).toContain(name);
const click=(page,x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});

test('reload and address-bar shortcuts remain available with game focus',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  // Synthetic events cannot invoke browser chrome. They do exercise the real
  // installed SDL listeners and expose whether those cancel the browser action.
  const results=await page.evaluate(()=>{
    const canvas=document.getElementById('canvas');canvas.focus();
    const results=[];
    for(const modifier of ['ctrlKey','metaKey'])for(const key of ['r','l']) {
      for(const shiftKey of [false,true]) {
        const options={key,code:'Key'+key.toUpperCase(),[modifier]:true,shiftKey,
          bubbles:true,cancelable:true};
        for(const type of ['keydown','keypress']) {
          const event=new KeyboardEvent(type,options);
          canvas.dispatchEvent(event);
          results.push({modifier,key,shiftKey,type,cancelled:event.defaultPrevented});
        }
        canvas.dispatchEvent(new KeyboardEvent('keyup',options));
      }
    }
    return results;
  });
  expect(results.filter(result=>result.cancelled)).toEqual([]);
  // Ordinary game controls still reach SDL after the shortcut attempts.
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
  await page.locator('#canvas').press('Escape',{delay:80});
  await screen(page,'MainMenuScreen');
});

test('click coordinates stay correct when browser motion delivery is missing',async({page})=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
  // Inject a lost/coalesced motion delivery, while keeping actual player button
  // input. SDL must receive the button's own position, not its last hover point.
  await page.evaluate(()=>document.addEventListener('mousemove',event=>{
    if(event.isTrusted)event.stopImmediatePropagation();
  },true));
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.setViewportSize({width:1000,height:700});
  await expect.poll(async()=>(await state(page)).width).toBe(1000);
  await page.locator('#canvas').press('Escape',{delay:80});
  await expect.poll(()=>require('./pixels').hasLightText(page,{x:360,y:382,width:280,height:34})).toBe(true);
  await click(page,500,400);await screen(page,'EndGameScreen');
});

test('desktop right-click cancels a selection and cycles sidebar views without a browser menu',async({page},info)=>{
  await page.goto(gameURL());await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page);
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.evaluate(()=>{
    window.canvasContextMenus=[];
    document.addEventListener('contextmenu',event=>{
      if(event.target.id==='canvas')window.canvasContextMenus.push(event.defaultPrevented);
    });
  });
  const panel={x:1042,y:185,width:150,height:240};
  await page.mouse.move(400,350);
  const construction=await page.screenshot({clip:panel});
  await page.mouse.click(400,350,{button:'right',delay:80});
  await page.waitForTimeout(160);
  const flags=await page.screenshot({clip:panel});
  expect(flags.equals(construction)).toBe(false);
  await info.attach('right-click-flags',{body:flags,contentType:'image/png'});
  for(let i=0;i<3;i++)await page.mouse.click(400,350,{button:'right',delay:80});
  await page.waitForTimeout(160);
  expect((await page.screenshot({clip:panel})).equals(construction)).toBe(true);
  // Select the first building tool, then cancel it with a secondary click.
  await click(page,1075,205);
  await page.mouse.click(400,350,{button:'right',delay:80});
  await page.waitForTimeout(160);
  expect((await page.screenshot({clip:panel})).equals(construction)).toBe(true);
  const menus=await page.evaluate(()=>window.canvasContextMenus);
  expect(menus.length).toBeGreaterThan(0);
  expect(menus.every(Boolean)).toBe(true);
});
