const {chromium}=require('../../browser/node_modules/playwright');
const {clickMainMenu,clickSettingsDone}=require('../../browser/tests/main-menu');
(async()=>{
 const browser=await chromium.launch({headless:true});
 const context=await browser.newContext({viewport:{width:1200,height:900}});
 const page=await context.newPage();
 const screen=async(name)=>page.waitForFunction(n=>glob2Diagnostics.snapshot().screen.includes(n),name);
 const click=async(x,y)=>page.locator('#canvas').click({position:{x,y},delay:80});
 await page.goto('http://127.0.0.1:19880/?renderer=software');await screen('MainMenuScreen');
 await clickMainMenu(page,'settings');await screen('SettingsScreen');
 await clickSettingsDone(page);await screen('MainMenuScreen');
 await page.waitForFunction(()=>glob2Diagnostics.snapshot().persistence==='persisted');
 await clickMainMenu(page,'settings');await screen('SettingsScreen');
 await click(208,233);
 await page.evaluate(()=>{
  const sync=FS.syncfs; window.reviewWrites=0;
  FS.syncfs=function(populate,callback){
   if(populate)return sync.call(FS,populate,callback);
   if(++window.reviewWrites===1)sync.call(FS,false,error=>{window.releaseFirstWrite=()=>callback(error);});
   else window.blockedSecondWrite=true;
  };
 });
 await click(700,224);
 await page.waitForFunction(()=>typeof window.releaseFirstWrite==='function');
 await click(700,224);
 await clickSettingsDone(page);
 console.log('Before release:',await page.evaluate(()=>({state:glob2Diagnostics.snapshot(),preferences:glob2Diagnostics.preferences()})));
 await page.evaluate(()=>window.releaseFirstWrite());
 await screen('MainMenuScreen');
 console.log('Closed with pending newer save:',await page.evaluate(()=>({state:glob2Diagnostics.snapshot(),preferences:glob2Diagnostics.preferences(),blockedSecondWrite})));
 await page.screenshot({path:'artifacts/pr379-review/settings-race.png'});
 await page.reload();await screen('MainMenuScreen');
 console.log('Reloaded preferences:',await page.evaluate(()=>glob2Diagnostics.preferences()));
 await browser.close();
})().catch(error=>{console.error(error);process.exit(1)});
