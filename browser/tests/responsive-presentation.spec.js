const {test,expect}=require('@playwright/test');
const {gameURL,clickMainMenu,clickCustomGameStart,clickSettingsDone}=require('./main-menu');
const snapshot=page=>page.evaluate(()=>glob2Diagnostics.snapshot());
const screen=(page,name)=>expect.poll(async()=>(await snapshot(page)).screen).toContain(name);
async function matchFrame(page) {
  const frames=(await snapshot(page)).frames;
  await expect.poll(async()=>(await snapshot(page)).frames).toBeGreaterThan(frames+2);
}
async function touchMatch(page,x,y) {await page.touchscreen.tap(x,y);await matchFrame(page);}

test.describe('responsive mixed input',()=>{
  test.use({viewport:{width:1024,height:768},hasTouch:true});
  test('native text editing preserves Unicode and selection across rotation',async({page},info)=>{
    await page.goto(gameURL());await screen(page,'MainMenuScreen');
    expect(await page.evaluate(()=>Module.presentationMetrics.touch)).toBe(true);
    await clickMainMenu(page,'settings');await screen(page,'SettingsScreen');
    const canvas=page.locator('#canvas');
    for(let i=0;i<6;++i) await canvas.press('Tab');
    await canvas.press('Enter');
    const field=page.locator('input[aria-label="Game text field"]');
    await expect(field).toBeVisible();
    await field.click();await field.fill('Zoë 🌱');
    await expect.poll(()=>page.evaluate(()=>(FS.analyzePath('/home/web_user/.glob2/preferences.txt').exists ? FS.readFile('/home/web_user/.glob2/preferences.txt',{encoding:'utf8'}) : ''))).toContain('username=Zoë 🌱');
    await field.evaluate(element=>element.setSelectionRange(0,3));
    await field.press('Backspace');await field.pressSequentially('Ada');
    await expect(field).toHaveValue('Ada 🌱');
    await page.setViewportSize({width:768,height:1024});
    await expect(field).toHaveValue('Ada 🌱');
    await expect(field).toBeFocused();
    await expect.poll(async()=>{const box=await field.boundingBox();return box.x+box.width;}).toBeLessThanOrEqual(768);
    expect((await field.boundingBox()).x).toBeGreaterThanOrEqual(0);
    await page.screenshot({path:info.outputPath('tablet-text-rotation.png')});
    await page.evaluate(()=>{Module.presentationMetrics.keyboardInset=680;});
    await expect.poll(async()=>{const box=await field.boundingBox();return box.y+box.height;}).toBeLessThanOrEqual(344);
    await expect(field).toBeFocused();
    expect(await page.evaluate(()=>Module.presentationMetrics.height)).toBe(1024);
    await page.evaluate(()=>{Module.presentationMetrics.keyboardInset=0;});
    await expect.poll(()=>page.evaluate(()=>(FS.analyzePath('/home/web_user/.glob2/preferences.txt').exists ? FS.readFile('/home/web_user/.glob2/preferences.txt',{encoding:'utf8'}) : ''))).toContain('username=Ada 🌱');
    await expect.poll(async()=>(await field.boundingBox()).y).toBeGreaterThan(100);
    await clickSettingsDone(page);await screen(page,'MainMenuScreen');
    await page.reload();await screen(page,'MainMenuScreen');
    expect(await page.evaluate(()=>(FS.analyzePath('/home/web_user/.glob2/preferences.txt').exists ? FS.readFile('/home/web_user/.glob2/preferences.txt',{encoding:'utf8'}) : ''))).toContain('username=Ada 🌱');
  });
  test('touch setup and gameplay retain their session when resized with a mouse attached',async({page},info)=>{
    await page.goto(gameURL());await screen(page,'MainMenuScreen');
    await clickMainMenu(page,'custom');await screen(page,'CustomGameScreen');
    await clickCustomGameStart(page);
    await expect.poll(async()=>(await snapshot(page)).tick,{timeout:60000}).toBeGreaterThan(20);
    const tick=(await snapshot(page)).tick;
    await page.setViewportSize({width:390,height:844});
    await expect.poll(async()=>{const s=await snapshot(page);return [s.width,s.height];}).toEqual([390,844]);
    await matchFrame(page);
    await page.mouse.move(200,200);
    await expect.poll(async()=>(await snapshot(page)).tick).toBeGreaterThan(tick);
    expect((await snapshot(page)).screenClass).toContain('GameSessionScreen');
    await page.screenshot({path:info.outputPath('phone-gameplay-mouse.png')});
    // A real touch selects the first construction row and starts a preview.
    await touchMatch(page,32,820);
    await touchMatch(page,180,540);
    await touchMatch(page,195,400);
    await page.screenshot({path:info.outputPath('phone-placement-preview.png')});
    await page.setViewportSize({width:844,height:390});
    await expect.poll(()=>page.evaluate(()=>Module.presentationMetrics.width)).toBe(844);
    await expect.poll(async()=>{const s=await snapshot(page);return [s.width,s.height];}).toEqual([844,390]);
    await matchFrame(page);
    await page.screenshot({path:info.outputPath('phone-gameplay-landscape.png')});
    // Cancel the retained preview, then save through the responsive dialog.
    await touchMatch(page,630,366);
    await page.locator('#canvas').press('Escape');
    await matchFrame(page);
    await page.touchscreen.tap(420,88);
    const field=page.locator('input[aria-label="Game text field"]');
    await expect(field).toBeVisible();await field.fill('Responsive phone');
    await page.screenshot({path:info.outputPath('phone-save-dialog.png')});
    await page.touchscreen.tap(260,302);
    const digest=()=>page.evaluate(()=>glob2Diagnostics.saveDigest('Responsive_phone.game'));
    await expect.poll(digest).not.toBeNull();
    await expect.poll(async()=>(await snapshot(page)).persisting).toBe(false);
    const saved=await digest();
    const bytes=await page.evaluate(()=>Array.from(FS.readFile('/home/web_user/.glob2/games/Responsive_phone.game')));
    require('node:fs').writeFileSync(info.outputPath('Responsive_phone.game'),Buffer.from(bytes));
    await page.reload();await screen(page,'MainMenuScreen');
    expect(await digest()).toEqual(saved);
    await clickMainMenu(page,'load');await screen(page,'ChooseMapScreen');
    await page.touchscreen.tap(300,88);
    await page.locator('#canvas').press('Enter');
    await screen(page,'match');
    await expect.poll(async()=>(await snapshot(page)).tick).toBeGreaterThan(tick);
    await page.screenshot({path:info.outputPath('phone-loaded-match.png')});
  });
});
