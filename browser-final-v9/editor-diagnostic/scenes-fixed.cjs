require('../guard.cjs').guard();
const path=require('node:path'),fs=require('node:fs');
const root=path.resolve(__dirname,'../../../..');
const {chromium,expect}=require(path.join(root,'browser/node_modules/@playwright/test'));
const {clickMainMenu,clickControl,clickSettingsDone,tapControl,control}=require(path.join(root,'browser/tests/main-menu'));
const {clickCreateMap}=require(path.join(root,'browser/tests/editor-controls'));
const report=[];
async function snapshot(page){return page.evaluate(()=>({diagnostics:glob2Diagnostics.snapshot(),dpr:devicePixelRatio,css:{width:innerWidth,height:innerHeight},canvas:[...document.querySelectorAll('canvas')].map(c=>({width:c.width,height:c.height,rect:{x:c.getBoundingClientRect().x,y:c.getBoundingClientRect().y,width:c.getBoundingClientRect().width,height:c.getBoundingClientRect().height}}))}));}
async function capture(page,name){await page.screenshot({path:path.join(__dirname,name+'.png')});report.push({name,state:await snapshot(page)});fs.writeFileSync(path.join(__dirname,'scenes-report.json'),JSON.stringify(report,null,2));}
async function boot(page){await page.goto('http://127.0.0.1:42771/?renderer=software&threads=serial');await expect.poll(async()=>{const s=await page.evaluate(()=>glob2Diagnostics.snapshot()),n=s.screen+' '+s.screenClass;for(const key of ['audio.silence','hand.right','intro/menu'])if(s.controls[key]?.enabled){await clickControl(page,key);return false;}return n.includes('MainMenuScreen');},{timeout:180000}).toBe(true);}
async function screen(page,name){await expect.poll(()=>page.evaluate(()=>{const s=glob2Diagnostics.snapshot();return s.screen+' '+s.screenClass}),{timeout:180000}).toContain(name);}
async function captureStatistics(page,name,touch,viewport){
 await clickControl(page,'intro/help'); // Close the help card opened by the preceding capture.
 const hasStats=()=>page.evaluate(()=>Boolean(glob2Diagnostics.snapshot().controls['stats/topic/0']));
 if(!await hasStats()){
  if(touch){
   // This is the real Tools action; hand-painted lens targets need actual-image inspection.
   await page.touchscreen.tap(97,770);
   await capture(page,name+'-stats-tools');const controls=await page.evaluate(()=>glob2Diagnostics.snapshot().controls);if(controls['touch/lens/3'])await clickControl(page,'touch/lens/3');else{const instruction=path.join(__dirname,name+'-actual-lens-click.json');for(let i=0;i<120&&!fs.existsSync(instruction);i++)await page.waitForTimeout(1000);if(!fs.existsSync(instruction))throw new Error('Actual-image lens click review not provided');const point=JSON.parse(fs.readFileSync(instruction));await page.touchscreen.tap(point.x,point.y);}await control(page,'stats/topic/0');
  }else{
   // Normal right-click cycles views and clears selection; no state injection or hidden flags.
   for(let attempt=0;attempt<6 && !await hasStats();++attempt){await page.mouse.click(viewport.width*.48,viewport.height*.65,{button:'right'});await page.waitForTimeout(250);}
  }
 }
 if(!await hasStats()){report.push({name:name+'-statistics',status:'pending actual-image navigation or unavailable through normal tutorial UI; no visibility flags changed',touch});fs.writeFileSync(path.join(__dirname,'scenes-report.json'),JSON.stringify(report,null,2));return;}
 await clickControl(page,'stats/topic/0');await capture(page,name+'-stats-overview');
 let keys=await page.evaluate(()=>Object.keys(glob2Diagnostics.snapshot().controls));expect(keys.some(k=>k.startsWith('stats/metric/') && /win.probability/i.test(k))).toBe(false);
 if(keys.includes('stats/full-history/population')){await clickControl(page,'stats/full-history/population');await control(page,'stats/history/close');await capture(page,name+'-stats-population-history');await clickControl(page,'stats/history/close');}
 await clickControl(page,'stats/topic/4');await capture(page,name+'-stats-world');
 keys=await page.evaluate(()=>Object.keys(glob2Diagnostics.snapshot().controls));expect(keys.some(k=>k.startsWith('stats/metric/') && /win.probability/i.test(k))).toBe(false);
 report.push({name:name+'-statistics-policy',status:'actual live player UI only; hidden win-probability metric absent; no observer/profile flags modified'});fs.writeFileSync(path.join(__dirname,'scenes-report.json'),JSON.stringify(report,null,2));
}
(async()=>{const browser=await chromium.launch();try{const context=await browser.newContext({locale:'bg-BG',viewport:{width:390,height:844},hasTouch:true});const page=await context.newPage();await boot(page);await clickMainMenu(page,'settings');await clickControl(page,'nav.current');await clickControl(page,'popup/0');await clickControl(page,'display.textsize');await clickControl(page,'popup/2');await clickSettingsDone(page);await clickMainMenu(page,'editor');await screen(page,'EditorMainMenu');await clickControl(page,'new-map');await screen(page,'NewMapScreen');await clickCreateMap(page);await screen(page,'MapEditorScreen');await page.waitForTimeout(300);await page.locator('#canvas').press('Escape',{delay:80});await page.waitForTimeout(300);await capture(page,'phone150-editor-after-escape');let keys=await page.evaluate(()=>Object.keys(glob2Diagnostics.snapshot().controls));if(!keys.includes('return')){await page.touchscreen.tap(35,22);await page.waitForTimeout(300);await capture(page,'phone150-editor-visible-menu-button');}await capture(page,'phone150-editor-menu-actual');keys=await page.evaluate(()=>Object.keys(glob2Diagnostics.snapshot().controls));fs.writeFileSync(path.join(__dirname,'control-state.json'),JSON.stringify({keys,method:'Actual Escape, then visible Menu button if menu return absent; no simulation injection'},null,2));await context.close();}finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1});
