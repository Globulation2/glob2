require('./guard.cjs').guard();
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../..');
const {chromium,expect}=require(path.join(root,'browser/node_modules/@playwright/test'));
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const bg=JSON.parse(fs.readFileSync(path.join(root,'platform/packages/i18n/locales/bg.json')));
(async()=>{const browser=await chromium.launch();const reports=[];try{
 for(const[name,viewport,deviceScaleFactor]of[['desktop',{width:1200,height:900},1],['desktop-dpr2',{width:1200,height:900},2],['phone',{width:390,height:844},1]]){
  const context=await browser.newContext({locale:'bg-BG',viewport,deviceScaleFactor});const page=await context.newPage();const pending=[],network=[];
  page.on('response',response=>pending.push((async()=>{const url=new URL(response.url());if(url.origin!=='http://127.0.0.1:42771')return;const row={path:url.pathname,status:response.status(),headers:await response.allHeaders()};try{const body=await response.body();row.decoded_bytes=body.length;row.decoded_sha256=sha(body);}catch(e){row.body_error=e.message;}network.push(row);})()));
  await page.route('**/assets/core*.data',route=>route.fulfill({status:503,body:''}));await page.goto('http://127.0.0.1:42771/?renderer=software&threads=serial');
  await expect(page.locator('html')).toHaveAttribute('lang','bg');await expect(page.locator('#loading-status')).toHaveText(bg['The game could not be downloaded.']);
  const fonts=await page.evaluate(async()=>{await document.fonts.ready;const loaded=await document.fonts.load('16px "Glob2 Bulgarian"','ѝ Ѝ ѝ Ѝ — „Български език“; «цитат», … ! ?');const el=document.querySelector('#loading-status');return{family:getComputedStyle(el).fontFamily,loaded:loaded.map(f=>({family:f.family,status:f.status,unicodeRange:f.unicodeRange})),rect:el.getBoundingClientRect().toJSON(),scrollWidth:el.scrollWidth,clientWidth:el.clientWidth,dpr:devicePixelRatio};});
  assert.match(fonts.family,/Glob2 Bulgarian/);assert.ok(fonts.loaded.length>0&&fonts.loaded.every(f=>f.status==='loaded'));
  const cdp=await context.newCDPSession(page);await cdp.send('DOM.enable');await cdp.send('CSS.enable');const doc=await cdp.send('DOM.getDocument');const node=await cdp.send('DOM.querySelector',{nodeId:doc.root.nodeId,selector:'#loading-status'});const platform=await cdp.send('CSS.getPlatformFontsForNode',{nodeId:node.nodeId});
  assert.ok(platform.fonts.length>0);assert.ok(platform.fonts.every(f=>f.isCustomFont&&f.glyphCount>0),'All status glyphs must use actual custom webfont, not installed OS fallback');
  await Promise.allSettled(pending);const ttf=network.find(r=>r.path==='/fonts/sans-core.ttf'&&r.status===200);assert.ok(ttf,'Actual startup webfont network response missing');assert.equal(ttf.decoded_sha256,sha(fs.readFileSync(path.join(root,'browser/assets/sans-core.ttf'))));assert.match(ttf.headers['content-type'],/font\/ttf/);assert.equal(ttf.headers['content-encoding'],'gzip');
  const png=path.join(__dirname,`startup-${name}.png`);await page.screenshot({path:png});reports.push({name,viewport,deviceScaleFactor,fonts,actual_platform_fonts:platform.fonts,network,png:path.basename(png),png_sha256:sha(fs.readFileSync(png)),scope:'Intentional core503 before WASM; actual custom-font glyphs measured by Chromium CDP. No game runtime claimed.'});fs.writeFileSync(path.join(__dirname,'startup-report.json'),JSON.stringify({browser:browser.version(),reports},null,2));await context.close();
 }
}finally{await browser.close();}})().catch(e=>{fs.writeFileSync(path.join(__dirname,'startup-error.txt'),e.stack||String(e));console.error(e);process.exitCode=1;});
