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
  await clickCustomGameStart(page); // A fresh profile has a valid premade map preselected.
  await expect.poll(async()=>(await state(page)).tick).toBeGreaterThan(25);
  await page.setViewportSize({width:1000,height:700});
  await expect.poll(async()=>(await state(page)).width).toBe(1000);
  await page.locator('#canvas').press('Escape',{delay:80});
  await expect.poll(()=>require('./pixels').hasLightText(page,{x:360,y:382,width:280,height:34})).toBe(true);
  await click(page,500,400);await screen(page,'EndGameScreen');
});
