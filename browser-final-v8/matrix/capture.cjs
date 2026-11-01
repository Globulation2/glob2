require('../guard.cjs').guard();
const path = require('node:path');
const fs = require('node:fs');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../../../..');
const {chromium,expect} = require(path.join(root,'browser/node_modules/@playwright/test'));
const {waitForMainMenu,clickMainMenu,clickControl} = require(path.join(root,'browser/tests/main-menu'));
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
(async()=>{
 const browser = await chromium.launch();
 const reports=[];
 try {
 for(const [name,viewport,deviceScaleFactor] of [['desktop',{width:1200,height:900},1],['phone',{width:390,height:844},1],['desktop-dpr2',{width:1200,height:900},2]]){
  const context = await browser.newContext({locale:'bg-BG',viewport,deviceScaleFactor,hasTouch:name==='phone'});
  const page = await context.newPage();
  const network=[],errors=[],pending=[];
  page.on('pageerror',e=>errors.push(e.message));
  page.on('response',res=>pending.push((async()=>{
    const u=new URL(res.url());
    if(u.origin!=='http://127.0.0.1:42771')return;
    const headers=await res.allHeaders();
    const row={url:u.pathname,status:res.status(),headers};
    if(/\.(wasm|ttf|json|data)$/.test(u.pathname)){
     try{const body=await res.body();row.decodedBytes=body.length;row.decodedSha256=sha(body);}catch(e){row.bodyError=e.message;}
    }
    network.push(row);
  })()));
  await page.goto('http://127.0.0.1:42771/?renderer=software&threads=serial');
  await expect.poll(async()=>{const state=await page.evaluate(()=>glob2Diagnostics.snapshot());for(const key of ['audio.silence','hand.right','intro/menu'])if(state.controls[key]?.enabled){await clickControl(page,key);return false;}return(state.screen+' '+state.screenClass).includes('MainMenuScreen');},{timeout:180000}).toBe(true);
  await page.screenshot({path:path.join(__dirname,`${name}-serial-main.png`)});
  const menu=await page.evaluate(()=>glob2Diagnostics.snapshot().controls);
  if(!menu['menu/6'] && !menu['menu/settings'] && menu['menu/more']) await clickControl(page,'menu/more');
  const state=await page.evaluate(()=>({mode:Module.executionMode,isolated:crossOriginIsolated,diagnostics:{...glob2Diagnostics.snapshot(),controls:undefined},catalog:Array.from(FS.readFile('/data/texts.bg.txt')),dimensions:{dpr:devicePixelRatio,innerWidth,innerHeight,canvas:[...document.querySelectorAll('canvas')].map(c=>({width:c.width,height:c.height,cssWidth:c.getBoundingClientRect().width,cssHeight:c.getBoundingClientRect().height}))},controls:glob2Diagnostics.snapshot().controls}));
  const native=Buffer.from(state.catalog);delete state.catalog;state.nativeCatalogSha256=sha(native);
  assert.equal(state.nativeCatalogSha256,sha(fs.readFileSync(path.join(root,'data/texts.bg.txt'))));
  assert.equal(state.mode,'serial');
  state.settingsLabel=(state.controls['menu/6']||state.controls['menu/settings'])?.label;
  assert.equal(state.settingsLabel,'Настройки');
  const fontBytes=await page.evaluate(()=>Array.from(FS.readFile('/data/fonts/sans.ttf')));
  state.nativeFontSha256=sha(Buffer.from(fontBytes));
  state.nativeFontIdentity=['data/fonts/sans.ttf','browser/assets/sans-core.ttf'].find(p=>sha(fs.readFileSync(path.join(root,p)))===state.nativeFontSha256);
  assert.ok(state.nativeFontIdentity,'native VFS font must match full or staged core font');
  assert.equal(state.isolated,true);
  await clickMainMenu(page,'settings');
  if(await page.evaluate(()=>Boolean(glob2Diagnostics.snapshot().controls['nav.5'])))await clickControl(page,'nav.5');else{await clickControl(page,'nav.current');const key=await page.evaluate(()=>Object.entries(glob2Diagnostics.snapshot().controls).find(([k,v])=>k.startsWith('popup/')&&v.label==='Език и играч')?.[0]);assert.ok(key,'Actual compact Language and player category');await clickControl(page,key);}
  await page.screenshot({path:path.join(__dirname,`${name}-serial-language.png`)});
  state.languageControls=await page.evaluate(()=>glob2Diagnostics.snapshot().controls);
  await Promise.allSettled(pending);
  for(const row of network.filter(r=>/\.(wasm|ttf|data|json)$/.test(r.url) && r.status===200)) {
    assert.equal(row.headers['content-encoding'],'gzip',row.url);
    if(row.url.endsWith('.wasm')) assert.match(row.headers['content-type'],/application\/wasm/,row.url);
  }
  reports.push({name,viewport,deviceScaleFactor,state,errors,network});fs.writeFileSync(path.join(__dirname,'capture-report.json'),JSON.stringify({browser:browser.version(),reports},null,2));
  await context.close();
 }
 fs.writeFileSync(path.join(__dirname,'capture-report.json'),JSON.stringify({browser:browser.version(),reports},null,2));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
