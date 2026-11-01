import { chromium } from '../../../platform/node_modules/@playwright/test/index.mjs';
import { readFile, writeFile } from 'node:fs/promises';
const output=new URL('./after/',import.meta.url);
const base='http://127.0.0.1:42769';
const bg=JSON.parse(await readFile(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
const english=JSON.parse(await readFile(new URL('../../../platform/packages/i18n/locales/en.json',import.meta.url),'utf8'));
let seed;for(let attempt=0;attempt<180;attempt++){try{seed=await (await fetch(base+'/__seed')).json();break}catch{await new Promise(r=>setTimeout(r,1000))}}if(!seed)throw new Error('Isolated seeded server failed to start');
const browser=await chromium.launch({headless:true});
const reports=[];
try {
 for (const [mode,width,height] of [['desktop',1280,900],['phone',390,844]]) {
  const context=await browser.newContext({locale:'bg-BG',viewport:{width,height},reducedMotion:'reduce'});
  await context.addCookies([{name:'glob2_locale',value:'bg',url:base},{name:'glob2_session',value:seed.userSession,url:base}]);
  const page=await context.newPage();
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  const routes=['/maps','/sets','/ais','/buildings','/music','/generators','/players','/leaderboard','/matches',`/maps/${seed.mapId}`,`/players/${seed.accounts.kestrel}`,`/matches/${seed.featuredMatch}`,`/matches/${seed.pendingMatch}`,'/account','/sets/new','/ai-studio','/building-studio','/map-studio','/music-studio','/skins','/j/GLOBJOIN'];
  for (const route of routes) {
   const result={mode,route};const start=errors.length;
   try {
    await page.goto(base+route,{waitUntil:'networkidle',timeout:60000});
    await page.evaluate(()=>document.fonts.ready);
    result.heading=await page.locator('h1').allTextContents();
    result.panels=await page.locator('h2').allTextContents();
    result.metrics=await page.evaluate(()=>({lang:document.documentElement.lang,overflow:document.documentElement.scrollWidth-innerWidth,font:[...document.fonts].filter(f=>f.family.includes('Glob2 Bulgarian')).map(f=>({family:f.family,status:f.status})),family:getComputedStyle(document.querySelector('h1')??document.body).fontFamily}));
    result.body=await page.locator('body').innerText();
    result.english=await page.evaluate(({english,bg})=>{
      const matches=[];const walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
      while(walker.nextNode()){
       const parent=walker.currentNode.parentElement;if(!parent||parent.closest('script,style,option')||!parent.getClientRects().length)continue;
       const text=walker.currentNode.textContent.trim();
       if(text.length>3&&typeof english[text]==='string'&&typeof bg[text]==='string'&&bg[text]!==text)matches.push(text);
      }
      return [...new Set(matches)];
    },{english,bg});
    result.outside=await page.locator('main button:visible,main input:visible,main select:visible,main h1:visible,.library-card:visible').evaluateAll(es=>es.filter(e=>{const r=e.getBoundingClientRect();return r.width&& (r.left< -1||r.right>innerWidth+1)}).map(e=>({tag:e.tagName,text:(e.textContent??'').slice(0,90),left:e.getBoundingClientRect().left,right:e.getBoundingClientRect().right})).slice(0,10));
    const file=mode+'-'+route.replace(/^\//,'').replaceAll('/','-')+'.png';
    await page.screenshot({path:new URL(file,output).pathname,fullPage:true});result.screenshot=file;
    result.errors=errors.slice(start);
   }catch(e){result.error=String(e)}
   reports.push(result);console.log(JSON.stringify({mode,route,heading:result.heading,metrics:result.metrics,english:result.english,outside:result.outside,error:result.error}));
   await writeFile(new URL('report.json',output),JSON.stringify(reports,null,2));
  }
  await context.close();
 }
}finally{await browser.close()}
