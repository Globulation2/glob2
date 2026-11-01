import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
test('map and terrain unavailable and credit notices use Bulgarian without dispatching work',async({page,request,baseURL},info)=>{
 const seed=await(await request.get('/__seed')).json();await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.userSession,url:baseURL}]);
 await page.context().route('**/api/v1/instance',async r=>{const response=await r.fetch(),data=await response.json();await r.fulfill({response,json:{...data,features:[...new Set([...(data.features??[]),'map-studio','terrain-studio'])]}})});
 let enabled=true;const writes=[];
 await page.context().route(/\/api\/v1\/(?:map|terrain)-studio\//,r=>{
  const path=new URL(r.request().url()).pathname;if(r.request().method()!=='GET'){writes.push(path);return r.fulfill({status:405,json:{message:'Read-only fixture'}})}
  if(path.endsWith('/account'))return r.fulfill({json:{enabled,available:0,reserved:0,packs:[],usage:[],activeRequest:null,model:'Deterministic fixture',maxRequestCredits:3,balance:0,rate:{input:1,cachedInput:1,output:1}}});return r.fulfill({json:{items:[]}})
 });
 for(const [route,key,name,flag]of [['map','An available Map credit is needed to chat or build.','map-no-credit',true],['map','Generation is unavailable. Saved projects remain accessible.','map-unavailable',false],['terrain','Generation is unavailable.','terrain-unavailable',false]]){
  enabled=flag;await page.goto('/'+route+'-studio',{waitUntil:'networkidle'});await expect(page.locator('.studio-compose-help')).toContainText(bg[key]);await page.evaluate(()=>document.fonts.ready);expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);
  const out=new URL('./verified/screenshots/',import.meta.url);mkdirSync(out,{recursive:true});const shot=await page.screenshot();writeFileSync(new URL(info.project.name+'-new30-'+name+'.png',out),shot);await info.attach(name,{body:shot,contentType:'image/png'});
 }
 expect(writes).toEqual([]);
});
