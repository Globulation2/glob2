import {chromium,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {readFile,writeFile} from 'node:fs/promises';
const out=new URL('./',import.meta.url),base='http://127.0.0.1:42769';
const bg=JSON.parse(await readFile(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
const t=k=>bg[k]??k;
const seed=await(await fetch(base+'/__seed')).json();
const browser=await chromium.launch({headless:true});
const report=[];
try{
 const context=await browser.newContext({locale:'bg-BG',viewport:{width:1280,height:900}});
 await context.addCookies([{name:'glob2_locale',value:'bg',url:base},{name:'glob2_session',value:seed.userSession,url:base}]);
 const page=await context.newPage();
 await page.goto(base+'/building-studio',{waitUntil:'networkidle'});
 await page.getByRole('button',{name:t('Create a family'),exact:true}).click();
 await expect(page.getByRole('heading',{name:t('Building family editor'),exact:true})).toBeVisible();
 for(const [name,width,height] of [['desktop',1280,900],['phone',390,844]]){
  await page.setViewportSize({width,height});await page.evaluate(()=>document.fonts.ready);
  report.push({name,url:page.url(),heading:await page.locator('h1').allTextContents(),body:await page.locator('body').innerText(),metrics:await page.evaluate(()=>({overflow:document.documentElement.scrollWidth-innerWidth,font:[...document.fonts].filter(f=>f.family.includes('Glob2 Bulgarian')).map(f=>f.status)}))});
  await page.screenshot({path:new URL(name+'-building-family-editor.png',out).pathname,fullPage:true});
 }
 await writeFile(new URL('editor-report.json',out),JSON.stringify(report,null,2));
 await context.close();
}finally{await browser.close()}
