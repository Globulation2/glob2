import {chromium} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {readFile,writeFile} from 'node:fs/promises';
const out=new URL('./',import.meta.url),base='http://127.0.0.1:42769',seed=await(await fetch(base+'/__seed')).json();
const editor=JSON.parse(await readFile(new URL('editor-report.json',out),'utf8'))[0].url;
const browser=await chromium.launch({headless:true});const reports=[];
try{
 const context=await browser.newContext({locale:'bg-BG',viewport:{width:390,height:844}});
 await context.addCookies([{name:'glob2_locale',value:'bg',url:base},{name:'glob2_session',value:seed.userSession,url:base}]);
 const page=await context.newPage();
 for(const route of [`/players/${seed.accounts.kestrel}`]){
  await page.goto(base+route,{waitUntil:'networkidle'});
  reports.push({route,elements:await page.evaluate(()=>[...document.querySelectorAll('main *')].filter(e=>{const r=e.getBoundingClientRect();return r.width>0&&r.right>innerWidth+1&&r.left>=0}).map(e=>{const r=e.getBoundingClientRect(),s=getComputedStyle(e);return {tag:e.tagName,class:e.className,text:(e.textContent??'').trim().slice(0,110),right:r.right,width:r.width,whiteSpace:s.whiteSpace,minWidth:s.minWidth,maxWidth:s.maxWidth,display:s.display}}).slice(0,40))});
 }
 await writeFile(new URL('player-overflow-before.json',out),JSON.stringify(reports,null,2));
 await context.close();
}finally{await browser.close()}
