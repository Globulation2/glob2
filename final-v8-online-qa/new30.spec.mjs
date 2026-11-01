import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
const money='Discussion spends no credits; 1 available Music credit is required. Creation requests build automatically · 1 credit on delivery. Includes all three moods.';
async function baseFixture(page,request,baseURL,features=[],account){
 const seed=await(await request.get('/__seed')).json();
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},...(account?[{name:'glob2_session',value:seed[account],url:baseURL}]:[])]);
 await page.context().route('**/api/v1/instance',async r=>{const response=await r.fetch(),data=await response.json();await r.fulfill({response,json:{...data,features:[...new Set([...(data.features??[]),...features])]}})});
 return seed;
}
async function capture(page,info,name){
 await page.evaluate(()=>document.fonts.ready);await expect(page.locator('html')).toHaveAttribute('lang','bg');
 expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);
 expect(await page.evaluate(()=>[...document.fonts].some(f=>f.family.includes('Glob2 Bulgarian')&&f.status==='loaded'))).toBe(true);
 const out=new URL('./verified/screenshots/',import.meta.url);mkdirSync(out,{recursive:true});const shot=await page.screenshot({fullPage:false});
 writeFileSync(new URL(info.project.name+'-'+name+'.png',out),shot);await info.attach(name,{body:shot,contentType:'image/png'});
}
test('new administrator chart labels preserve actual seeded activity facts',async({page,request,baseURL},info)=>{
 await baseFixture(page,request,baseURL,[],'adminSession');await page.goto('/admin/overview',{waitUntil:'networkidle'});
 const accounts=page.getByRole('figure',{name:bg['Daily active accounts']+' · '+bg['Accounts per day'],exact:true});
 await expect(accounts).toBeVisible();for(const k of ['Guest accounts','Registered accounts'])await expect(accounts.getByText(bg[k],{exact:true}).first()).toBeAttached();
 await accounts.scrollIntoViewIfNeeded();await capture(page,info,'new30-admin-accounts-chart');
 const matches=page.getByRole('figure',{name:bg['Match activity']+' · '+bg['Matches per day'],exact:true});await expect(matches).toBeVisible();await expect(matches.getByText(bg.Started,{exact:true}).first()).toBeAttached();await matches.scrollIntoViewIfNeeded();await capture(page,info,'new30-admin-started-chart');
});
test('skin view callbacks, blank generated name and shop names translate without purchasing',async({page,request,baseURL},info)=>{
 await baseFixture(page,request,baseURL,['skins.designer','skins.sales']);
 await page.context().route('**/api/v1/skins/products',r=>r.fulfill({json:{testMode:true,items:['stripes','spots'].map(sku=>({sku,name:'Colony '+sku,available:false,amount:null,currency:null}))}}));
 await page.context().route('**/api/v1/skins/purchases',r=>r.fulfill({json:{items:[]}}));
 await page.goto('/skins',{waitUntil:'networkidle'});const name=page.getByLabel(bg['Skin name'],{exact:true});await expect(name).toHaveValue(bg['My colony']);
 await name.fill('');await name.blur();await expect(name).toHaveValue(bg['Untitled skin']);
 const camera=page.getByRole('combobox',{name:bg['Camera view'],exact:true});
 for(const [id,key,angle] of [['front','Front view',0],['back','Back view',-180],['left','Left view',90],['right','Right view',-90]]){
  await expect(camera.locator(`option[value="${id}"]`)).toHaveText(bg[key]);await camera.selectOption(id);await expect(page.locator('.skin-view-compass > span')).toHaveAttribute('style',`transform: rotate(${angle}deg);`);
 }
 await capture(page,info,'new30-skin-views-untitled');
 await name.fill('Authored My colony / личен избор');await name.blur();await page.reload({waitUntil:'networkidle'});await expect(name).toHaveValue('Authored My colony / личен избор');
 await page.getByRole('group',{name:bg['Model to paint'],exact:true}).getByRole('button',{name:bg.Swarm,exact:true}).click();
 await expect(page.getByRole('group',{name:bg.Pose,exact:true})).toHaveCount(0);await expect(page.locator('.skin-pose-strip')).toContainText(bg['Final game view · {slot0}°'].replace('{slot0}','0'));await capture(page,info,'new30-swarm-static-branch');
 await page.goto('/skins?purchase=cancelled',{waitUntil:'networkidle'});const shop=page.getByRole('dialog',{name:bg['Skin shop'],exact:true});await expect(shop).toBeVisible();for(const k of ['Colony stripes','Colony spots'])await expect(shop.getByRole('heading',{name:bg[k],exact:true})).toBeVisible();await capture(page,info,'new30-shop-stripes-spots');
});
test('studio ideas insert translated prose and all credit notices stay read-only',async({page,request,baseURL},info)=>{
 await baseFixture(page,request,baseURL,['music-studio','map-studio','terrain-studio'],'userSession');let enabled=true,available=3;const writes=[];
 await page.context().route(/\/api\/v1\/(?:music|map|terrain)-studio\//,r=>{
  const path=new URL(r.request().url()).pathname;if(r.request().method()!=='GET'){writes.push(path);return r.fulfill({status:405,json:{message:'Read-only fixture'}})}
  if(path.endsWith('/account'))return r.fulfill({json:{enabled,available,reserved:0,packs:[],usage:[],activeRequest:null,model:'Deterministic fixture',maxRequestCredits:3,balance:3,rate:{input:1,cachedInput:1,output:1}}});
  if(path.endsWith('/threads')||path.endsWith('/projects'))return r.fulfill({json:{items:[]}});return r.fulfill({status:404,json:{message:'No such fixture'}})
 });
 await page.goto('/music-studio',{waitUntil:'networkidle'});await expect(page.locator('.studio-edit-target')).toContainText(bg['New soundtrack']);
 const ideas=[['Moss & morning light','A warm woodland piece with flute, harp and gentle pizzicato. Let the melody wander and return; combat adds quiet drums and a bassoon counterline.'],['Little clockwork garden','Soft glass bells and rounded synth pads, curious and unhurried. A playful answering melody, with a deeper pulse as the colony grows.'],['A waltz for the rain','A cozy folk waltz with recorder and kalimba. Sparse, breathing phrases for calm, a lilting rhythm for building, and warm low strings for combat.']];
 const prompt=page.getByRole('textbox',{name:bg['Your idea or next change'],exact:true});
 for(const [title,text] of ideas){await page.locator('.mu-prompt').filter({hasText:bg[title]}).click();await expect(prompt).toHaveValue(bg[text]);}
 await expect(page.locator('.studio-compose-help')).toContainText(bg[money]);await capture(page,info,'new30-music-ideas-pricing');
 available=0;await page.reload({waitUntil:'networkidle'});await expect(page.locator('.studio-compose-help')).toContainText(bg['An available Music credit is needed to chat or build.']);await capture(page,info,'new30-music-no-credit');
 enabled=false;await page.reload({waitUntil:'networkidle'});await expect(page.locator('.studio-compose-help')).toContainText(bg['Generation is unavailable. Saved music remains accessible.']);await capture(page,info,'new30-music-generation-unavailable');
 enabled=true;available=3;
 for(const [route,key] of [['map','Discussion spends no credits; 1 available Map credit is required. Creation requests build automatically · 1 credit on delivery.'],['terrain','Discussion spends no credits; 1 available Terrain credit is required. Creation requests build automatically · 1 credit on delivery.']]){
  await page.goto('/'+route+'-studio',{waitUntil:'networkidle'});await expect(page.locator('.studio-compose-help')).toContainText(bg[key]);await capture(page,info,'new30-'+route+'-pricing');
 }
 expect(writes).toEqual([]);await info.attach('read-only-provider-boundary',{body:Buffer.from(JSON.stringify({writes,fixture:'instancefeatures andwallet/readmodels only; no generation/payment calls'})),contentType:'application/json'});
});
