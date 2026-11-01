import {chromium} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {readFile,writeFile} from 'node:fs/promises';
const out=new URL('./',import.meta.url),base='http://127.0.0.1:42769';
const seed=await(await fetch(base+'/__seed')).json();
const source=await readFile(new URL('../../../platform/apps/web/e2e/libraries.spec.ts',import.meta.url),'utf8');
const fixtureText=source.slice(source.indexOf("const id ="),source.indexOf('const libraries =')).replace(/ satisfies (AiInfo|SetInfo|MusicRelease)/g,'');
const fixtures=Function(fixtureText+';return {ai,terrain,music,id}')();
const browser=await chromium.launch({headless:true}),reports=[];
try{
 for(const [name,width,height] of [['desktop',1280,900],['phone',390,844]]){
  const context=await browser.newContext({locale:'bg-BG',viewport:{width,height}});
  await context.addCookies([{name:'glob2_locale',value:'bg',url:base},{name:'glob2_session',value:seed.userSession,url:base}]);
  const page=await context.newPage();
  for(const [route,key] of [['sets','terrain'],['ais','ai'],['music','music']]){
   await page.route('**/api/v1/'+route+'*',r=>{if(new URL(r.request().url()).pathname==='/api/v1/'+route)return r.fulfill({json:{items:[fixtures[key]],next:null}});return r.continue()});
   await page.goto(base+'/'+route,{waitUntil:'networkidle'});await page.evaluate(()=>document.fonts.ready);
   reports.push({name,route,source:'Existing libraries.spec.ts populated-list fixtures; liveAPI session and app, mockedlist response only',body:await page.locator('body').innerText(),overflow:await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth),heading:await page.locator('h1').allTextContents()});
   await page.screenshot({path:new URL(name+'-'+route+'-populated-fixture.png',out).pathname,fullPage:true});
  }
  await context.close();
 }
 await writeFile(new URL('populated-report.json',out),JSON.stringify(reports,null,2));
}finally{await browser.close()}
