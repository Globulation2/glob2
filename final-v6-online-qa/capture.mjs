import {chromium,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
const base='http://127.0.0.1:42769',out=new URL('./verified/screenshots/',import.meta.url);
await mkdir(out,{recursive:true});
const bg=JSON.parse(await readFile(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
const seed=JSON.parse(await readFile(new URL('./verified/seed.json',import.meta.url),'utf8'));
const browser=await chromium.launch({headless:true}),reports=[];
try{
 const context=await browser.newContext({locale:'bg-BG',viewport:{width:390,height:844}});
 await context.addCookies([{name:'glob2_locale',value:'bg',url:base},{name:'glob2_session',value:seed.userSession,url:base}]);
 const page=await context.newPage();
 async function capture(name,fullPage=true){
  await page.evaluate(()=>document.fonts.ready);
  const metrics=await page.evaluate(()=>({lang:document.documentElement.lang,overflow:document.documentElement.scrollWidth-innerWidth,font:[...document.fonts].filter(f=>f.family.includes('Glob2 Bulgarian')).map(f=>f.status)}));
  const labels=await page.locator('.building-field > label').evaluateAll(elements=>elements.map(e=>{
   const bounds=e.getBoundingClientRect(),walker=document.createTreeWalker(e,NodeFilter.SHOW_TEXT);let fits=true,text='';
   while(walker.nextNode()){const value=walker.currentNode.textContent.trim();if(!value)continue;text+=value;const range=document.createRange();range.selectNodeContents(walker.currentNode);fits&&=[...range.getClientRects()].every(r=>r.left>=bounds.left-1&&r.right<=bounds.right+1)}
   return{text,fits};
  }));
  expect(metrics.overflow).toBeLessThanOrEqual(1);expect(labels.every(l=>l.fits)).toBe(true);
  reports.push({name,url:page.url(),metrics,labels,headings:await page.locator('h1').allTextContents()});
  await page.screenshot({path:new URL(name+'.png',out).pathname,fullPage});
 }
 await page.goto(base+'/building-studio',{waitUntil:'networkidle'});
 await page.getByRole('button',{name:bg['Create a family'],exact:true}).click();
 await expect(page.getByRole('textbox',{name:bg['Family name'],exact:true})).toHaveValue(bg['New building family']);
 await capture('phone-building-default');
 await page.setViewportSize({width:1280,height:900});await capture('desktop-building-default');
 await page.setViewportSize({width:390,height:844});
 for(const [route,name] of [['/sets/new','phone-set-default'],['/music/new','phone-music-fields'],['/maps/new','phone-map-visibility'],['/ais','phone-ai-tags']]){
  await page.goto(base+route,{waitUntil:'networkidle'});
  await expect(page.locator('h1').first()).toBeVisible();
  await capture(name);
  if(route==='/sets/new'){
   const addLabel=key=>bg['Add {slot0}'].replace('{slot0}',bg[key]);
   const terrain=page.getByRole('button',{name:addLabel('Terrain'),exact:true});
   await expect(terrain).toBeVisible();await terrain.click();await expect(page.getByRole('textbox',{name:bg.Name,exact:true})).toHaveValue(bg.Terrain+': grass');await terrain.scrollIntoViewIfNeeded();await capture('phone-add-terrain',false);
   await page.locator('.set-tabs').getByRole('button',{name:bg.Resources,exact:true}).click();
   const resource=page.getByRole('button',{name:addLabel('Resource'),exact:true});
   await expect(resource).toBeVisible();await resource.click();await expect(page.getByRole('textbox',{name:bg.Name,exact:true})).toHaveValue(bg.Resource+': trees');await resource.scrollIntoViewIfNeeded();await capture('phone-add-resource',false);
  }
 }
 await writeFile(new URL('./verified/screenshots/report.json',import.meta.url),JSON.stringify(reports,null,2)+'\n');
 await context.close();
}finally{await browser.close()}
