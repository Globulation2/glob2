const {test,expect}=require('@playwright/test');
const {tapControl,clickMainMenu,control,controlBox}=require('./main-menu');
const fs=require('node:fs');
const path=require('node:path');

test.use({viewport:{width:400,height:800},hasTouch:true});
test('shared training tutorial keeps touch actions usable across rotation',async({page})=>{
  test.setTimeout(300000);
  await page.goto('/?renderer=software');
  await expect.poll(async()=>{
    const s=await page.evaluate(()=>glob2Diagnostics.snapshot());
    const name=s.screen+' '+s.screenClass;
    if(name.includes('AudioPreferenceScreen') && s.controls['audio.silence']?.enabled) await tapControl(page,'audio.silence');
    else if(name.includes('HandPreferenceScreen') && s.controls['hand.right']?.enabled) await tapControl(page,'hand.right');
    else if(name.includes('IntroductionOfferScreen') && s.controls['intro/start']?.enabled) await tapControl(page,'intro/start');
    else if(name.includes('MainMenuScreen')) await clickMainMenu(page,'introduction');
    return s.screen==='match' || name.includes('GameSessionScreen');
  },{timeout:180000}).toBe(true);
  await expect.poll(()=>page.evaluate(()=>glob2Diagnostics.snapshot().tick),{timeout:60000}).toBeGreaterThan(32);
  const out=path.resolve(__dirname,'../../artifacts/training/browser-ui');fs.mkdirSync(out,{recursive:true});
  for(const [width,height,label] of [[400,800,'portrait'],[800,400,'landscape']]) {
    await page.setViewportSize({width,height});
    for(const key of ['intro/destination','intro/help','intro/collapse']) {
      await control(page,key);
      const b=await controlBox(page,key);
      expect(b.width).toBeGreaterThanOrEqual(43.5);expect(b.height).toBeGreaterThanOrEqual(43.5);
      expect(b.x).toBeGreaterThanOrEqual(0);expect(b.y).toBeGreaterThanOrEqual(0);
      expect(b.x+b.width).toBeLessThanOrEqual(width+.5);expect(b.y+b.height).toBeLessThanOrEqual(height+.5);
    }
    await tapControl(page,'intro/destination');
    await page.screenshot({path:path.join(out,label+'.png')});
    await tapControl(page,'intro/help');
    await page.screenshot({path:path.join(out,label+'-help.png')});
    await tapControl(page,'intro/help');
  }
});
