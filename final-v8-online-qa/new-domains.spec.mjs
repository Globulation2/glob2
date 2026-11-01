import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
async function capture(page,info,name){
 await page.evaluate(()=>document.fonts.ready);
 expect(await page.evaluate(()=>[...document.fonts].some(f=>f.family.includes('Glob2 Bulgarian')&&f.status==='loaded'))).toBe(true);
 expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth),name).toBeLessThanOrEqual(1);
 await expect(page.locator('html')).toHaveAttribute('lang','bg');
 const out=process.env.V7_CAPTURE_DIR?new URL('file://'+process.env.V7_CAPTURE_DIR+'/'):new URL('./verified/screenshots/',import.meta.url);mkdirSync(out,{recursive:true});
 const shot=await page.screenshot({fullPage:true});
 writeFileSync(new URL(info.project.name+'-'+name+'.png',out),shot);
 await info.attach(name,{body:shot,contentType:'image/png'});
 const keys=JSON.parse(readFileSync(new URL('./new-source-keys.json',import.meta.url),'utf8'));
 const receipt=await page.evaluate(({keys,bg})=>{const text=document.body.innerText,attributes=[...document.querySelectorAll('[aria-label],[title]')].flatMap(e=>[e.getAttribute('aria-label'),e.getAttribute('title')]).filter(Boolean);return {url:location.href,viewport:{width:innerWidth,height:innerHeight},overflow:document.documentElement.scrollWidth-innerWidth,matchedNewSources:keys.filter(key=>text.includes(bg[key])||attributes.includes(bg[key]))}}, {keys,bg});
 await info.attach(name+'-receipt',{body:Buffer.from(JSON.stringify(receipt,null,2)),contentType:'application/json'});
}
test('new skin model and material captions use Bulgarian catalog',async({page,baseURL},info)=>{
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);
 // Fixture only: current seeded deployment intentionally disables skins.designer.
 await page.route('**/api/v1/instance',async route=>{const response=await route.fetch();const data=await response.json();data.features=[...new Set([...(data.features??[]),'skins.designer'])];await route.fulfill({response,json:data})});
 await page.goto('/skins',{waitUntil:'networkidle'});
 await expect(page.getByLabel(bg['Skin name'],{exact:true})).toBeEnabled();
 await expect(page.getByLabel(bg['Skin name'],{exact:true})).toHaveValue(bg['My colony']);
 await expect(page.locator('.sidebar-auth')).toHaveAttribute('aria-label',bg['Sign in']);
 if(info.project.name.endsWith('phone')){const box=await page.locator('.sidebar-auth').boundingBox();expect(box.width).toBeGreaterThanOrEqual(44);expect(box.height).toBeGreaterThanOrEqual(44);}
 for(const key of ['Worker','Warrior','Explorer','Swarm'])await expect(page.getByRole('button',{name:bg[key],exact:true})).toBeVisible();
 await page.getByRole('button',{name:bg.Material,exact:true}).click();
 for(const key of ['Classic glossy','Matte','Riveted metal','Hairy','Woven fabric'])await expect(page.getByRole('radio',{name:bg[key],exact:true})).toBeAttached();
 await capture(page,info,'new-skin-materials');
});
test('new administrative tabs and finance filters render Bulgarian on real seeded API',async({page,request,baseURL},info)=>{
 const seed=await(await request.get('/__seed')).json();
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.adminSession,url:baseURL}]);
 for(const route of ['finances','accounts','audit']){
  await page.goto('/admin/'+route,{waitUntil:'networkidle'});
  await expect(page.getByRole('heading',{name:bg.Administration,exact:true})).toBeVisible();
  await expect(page.locator('.sidebar-auth')).toHaveAttribute('aria-label',bg['Sign out']);
  if(info.project.name.endsWith('phone')){const box=await page.locator('.sidebar-auth').boundingBox();expect(box.width).toBeGreaterThanOrEqual(44);expect(box.height).toBeGreaterThanOrEqual(44);}
  for(const key of ['Finances','Accounts','Audit'])await expect(page.getByRole('link',{name:bg[key],exact:true})).toBeVisible();
  await capture(page,info,'new-admin-'+route);
 }
});
test('all new music listening guidance is localized in actual published player UI fixture',async({page,baseURL},info)=>{
 const id='11111111-1111-4111-8111-111111111111';
 const source=['Listen for the musical structure and development.','Check that all three moods play correctly.','Listen for unexpected jumps in volume between moods.','Listen for a click, gap, or abrupt change when the loop repeats.','Listen for short phrases that repeat too mechanically.','Switch moods and listen for changes in timing or harmony.','Check that building and combat feel distinct from calm.','Listen for hiss or scratchiness in quiet passages.','Listen for muddy bass or harsh high frequencies.','Check that calm passages remain audible alongside game sound.','Listen for notes that cut off unexpectedly.'];
 const labels=['score','format','loudness','seam','repetition','alignment','contrast','noise','balance','audibility','dropout'];
 const release={id,ownerId:'22222222-2222-4222-8222-222222222222',metadata:{title:'Fixture listening guidance',artist:'Fixture composer',description:'Authored fixture',license:'CC0-1.0',credits:'Synthetic',sources:[],tags:[],aiGenerated:false},status:'published',createdAt:'2026-10-01T12:00:00Z',frames:480000,tracks:['calm','building','combat'].map(mood=>({mood,url:'/fixture-'+mood+'.opus',sha256:'a'.repeat(64),waveform:[0.2,0.6,0.4]})),warnings:[],inspection:null,validation:labels.map(label=>({id:label,label,status:'warn',attempt:1,measures:[]})),uploaded:[],coverUrl:null,likes:0,downloads:0,liked:false,hidden:false,error:null};
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);
 await page.route('**/api/v1/music/'+id,r=>r.fulfill({json:release}));
 await page.goto('/music/'+id,{waitUntil:'networkidle'});
 for(const key of source)await expect(page.getByText(bg[key],{exact:true})).toBeVisible();
 await capture(page,info,'new-music-guidance-fixture');
});
