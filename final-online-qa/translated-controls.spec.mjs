import { readFileSync } from 'node:fs';
import { test, expect } from '../../../platform/node_modules/@playwright/test/index.mjs';
const messages=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
const tags=['Balanced','Rush','Defensive','Economy','Expansion','Experimental','Beginner-friendly'];
const id='11111111-1111-4111-8111-111111111111';
const createdAt='2026-10-01T12:00:00Z';
const version={id,hash:'a'.repeat(64),label:'1.2.0',notes:'',profile:2,createdAt,downloads:186,downloadUrl:'/api/v1/ais/'+id+'/versions/'+id+'/file',validations:[]};
const ai={id,name:'Patient Gardener',description:'A colony builder with a steady food supply.',tags:tags.slice(0,5),visibility:'public',hidden:false,owner:{id,displayName:'Maple'},createdAt,updatedAt:createdAt,likes:42,downloads:231,liked:false,favourited:false,latestVersion:version};

test.beforeEach(async({page,request,baseURL})=>{
 const seed=await(await request.get('/__seed')).json();
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.userSession,url:baseURL}]);
});
async function verify(page,info,name){
 await page.evaluate(()=>document.fonts.ready);
 await expect(page.locator('html')).toHaveAttribute('lang','bg');
 expect(await page.evaluate(()=>[...document.fonts].some(f=>f.family.includes('Glob2 Bulgarian')&&f.status==='loaded'))).toBe(true);
 expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);
 await info.attach(name,{body:await page.screenshot({fullPage:true}),contentType:'image/png'});
}

async function assertInlineFits(control,info,name){
 const measurement=await control.evaluate(element=>{
  const style=getComputedStyle(element),context=document.createElement('canvas').getContext('2d');
  if(!context)throw new Error('Text measurement unavailable');
  context.font=`${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
  const text=element instanceof HTMLSelectElement?element.selectedOptions[0]?.textContent??'':element.getAttribute('placeholder')??'';
  return {text,width:context.measureText(text).width,available:element.clientWidth-parseFloat(style.paddingLeft)-parseFloat(style.paddingRight)};
 });
 await info.attach(name,{body:Buffer.from(JSON.stringify(measurement,null,2)),contentType:'application/json'});
 expect(measurement.width).toBeLessThanOrEqual(measurement.available+1);
}

test('AI filter, card, detail and publish labels translate while canonical tags stay stable',async({page},info)=>{
 await page.route('**/api/v1/ais*',r=>new URL(r.request().url()).pathname==='/api/v1/ais'?r.fulfill({json:{items:[ai]}}):r.continue());
 await page.route('**/api/v1/ais/'+id,r=>r.fulfill({json:{ai,versions:[version],viewer:{owner:false,moderator:false}}}));
 await page.goto('/ais',{waitUntil:'networkidle'});
 await expect(page.locator('.ai-tags[role="group"] button')).toHaveText(tags.map(t=>messages[t]));
 await expect(page.locator('.library-card .ai-tags .badge')).toHaveText(ai.tags.map(t=>messages[t]));
 await assertInlineFits(page.getByPlaceholder(messages['Search names, descriptions, authors…'],{exact:true}),info,'ai-search-placeholder-fit');
 const filtered=page.waitForRequest(r=>new URL(r.url()).pathname==='/api/v1/ais'&&new URL(r.url()).searchParams.get('tags')==='Rush');
 await page.getByRole('group',{name:messages['AI tags']}).getByRole('button',{name:messages.Rush,exact:true}).click();
 await filtered;
 await verify(page,info,'translated-ai-filter-card');
 await page.goto('/ais/'+id,{waitUntil:'networkidle'});
 await expect(page.locator('h1')).toHaveText(ai.name);
 await expect(page.locator('.ai-detail-grid .ai-tags .badge')).toHaveText(ai.tags.map(t=>messages[t]));
 await verify(page,info,'translated-ai-detail');
 await page.goto('/ais/new',{waitUntil:'networkidle'});
 await expect(page.locator('.ai-tags[role="group"] button')).toHaveText(tags.map(t=>messages[t]));
 await verify(page,info,'translated-ai-publish');
});

test('map visibility labels translate while protocol values remain public unlisted private',async({page},info)=>{
 await page.goto('/maps/new',{waitUntil:'networkidle'});
 const select=page.getByRole('combobox',{name:messages.Visibility,exact:true});
 await expect(select.locator('option')).toHaveText(['Public — listed for everyone','Unlisted — accessible by link','Private — only you'].map(k=>messages[k]));
 await expect(select).toHaveValue('unlisted');
 await assertInlineFits(select,info,'unlisted-default-fit');
 await select.selectOption('public');await expect(select).toHaveValue('public');
 await verify(page,info,'translated-map-visibility');
});

test('music author labels use Artist and attribution Credits and sources',async({page},info)=>{
 await page.goto('/music/new',{waitUntil:'networkidle'});
 for(const key of ['Title','Artist','Description','Credits & sources'])await expect(page.getByLabel(messages[key],{exact:true})).toBeVisible();
 await expect(page.locator('.music-create form > label').filter({hasText:/^(Title|Artist|Description|Credits)$/})).toHaveCount(0);
 await verify(page,info,'translated-music-attribution');
});


test('new set and building drafts initialize localized names without rewriting stored metadata',async({page,baseURL},info)=>{
 await page.goto('/sets/new',{waitUntil:'networkidle'});
 await expect(page.getByRole('textbox',{name:messages.Title,exact:true})).toHaveValue(messages['New set']);
 await verify(page,info,'localized-new-set-title');
 await page.goto('/building-studio',{waitUntil:'networkidle'});
 const created=page.waitForResponse(r=>new URL(r.url()).pathname==='/api/v1/building-drafts'&&r.request().method()==='POST');
 await page.getByRole('button',{name:messages['Create a family'],exact:true}).click();
 const draft=await(await created).json();
 expect(draft.name).toBe(messages['New building family']);
 await expect(page.getByRole('textbox',{name:messages['Family name'],exact:true})).toHaveValue(messages['New building family']);
 const experiment=page.getByRole('combobox',{name:messages['Required experiment'],exact:true});
 await expect(experiment.locator('option:checked')).toHaveText(messages['Always available']);
 await assertInlineFits(experiment,info,'always-available-fit');
 // An explicit author-supplied title persists; changing locale must not rewrite it.
 await page.getByRole('textbox',{name:messages['Family name'],exact:true}).fill('Fixture author title');
 const saved=page.waitForResponse(r=>new URL(r.url()).pathname==='/api/v1/building-drafts/'+draft.id&&r.request().method()==='PUT');
 await page.getByRole('button',{name:messages['Save draft'],exact:true}).click();
 expect((await saved).ok()).toBe(true);
 await page.reload({waitUntil:'networkidle'});
 await expect(page.getByRole('textbox',{name:messages['Family name'],exact:true})).toHaveValue('Fixture author title');
 await page.context().addCookies([{name:'glob2_locale',value:'en',url:baseURL}]);
 await page.reload({waitUntil:'networkidle'});
 await expect(page.getByRole('textbox',{name:'Family name',exact:true})).toHaveValue('Fixture author title');
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);
 await page.reload({waitUntil:'networkidle'});
 await expect(page.getByRole('textbox',{name:messages['Family name'],exact:true})).toHaveValue('Fixture author title');
 await verify(page,info,'localized-new-building-title');
});

test('terrain and resource Add labels translate while package collections stay canonical',async({page,baseURL},info)=>{
 await page.goto('/sets/new',{waitUntil:'networkidle'});
 const addLabel=key=>messages['Add {slot0}'].replace('{slot0}',messages[key]);
 await expect(page.getByRole('button',{name:addLabel('Terrain'),exact:true})).toBeVisible();
 await page.getByRole('button',{name:addLabel('Terrain'),exact:true}).click();
 await expect(page.getByRole('group',{name:messages['{slot0} properties'].replace('{slot0}',messages.Terrain),exact:true})).toBeVisible();
 await expect(page.getByRole('textbox',{name:messages.Name,exact:true})).toHaveValue(messages.Terrain+': grass');
 await verify(page,info,'translated-add-terrain');
 await page.locator('.set-tabs').getByRole('button',{name:messages.Resources,exact:true}).click();
 await expect(page.getByRole('button',{name:addLabel('Resource'),exact:true})).toBeVisible();
 await page.getByRole('button',{name:addLabel('Resource'),exact:true}).click();
 await expect(page.getByRole('group',{name:messages['{slot0} properties'].replace('{slot0}',messages.Resource),exact:true})).toBeVisible();
 await expect(page.getByRole('textbox',{name:messages.Name,exact:true})).toHaveValue(messages.Resource+': trees');
 const saved=page.waitForResponse(r=>new URL(r.url()).pathname==='/api/v1/set-drafts'&&r.request().method()==='POST');
 await page.getByRole('button',{name:messages['Save draft'],exact:true}).click();
 const response=await saved;expect(response.ok()).toBe(true);
 const body=response.request().postDataJSON();expect(body.terrains).toHaveLength(1);expect(body.resources).toHaveLength(1);
 const stored=await response.json();expect(stored.package.terrains).toHaveLength(1);expect(stored.package.resources).toHaveLength(1);
 expect(stored.package.terrains[0].name).toBe(messages.Terrain+': grass');expect(stored.package.terrains[0].base).toBe('grass');expect(stored.package.resources[0].presentation.name).toBe(messages.Resource+': trees');
 await expect(page).toHaveURL(new RegExp('/sets/drafts/'+stored.id));
 await page.locator('.set-tabs').getByRole('button',{name:messages.Resources,exact:true}).click();
 await expect(page.getByRole('textbox',{name:messages.Name,exact:true})).toHaveValue(messages.Resource+': trees');
 await page.getByRole('textbox',{name:messages.Name,exact:true}).fill('Fixture resource title');
 const updated=page.waitForResponse(r=>new URL(r.url()).pathname==='/api/v1/set-drafts/'+stored.id&&r.request().method()==='PUT');
 await page.getByRole('button',{name:messages['Save draft'],exact:true}).click();expect((await updated).ok()).toBe(true);
 await page.context().addCookies([{name:'glob2_locale',value:'en',url:baseURL}]);await page.reload({waitUntil:'networkidle'});
 await expect(page.getByRole('textbox',{name:'Name',exact:true})).toHaveValue(messages.Terrain+': grass');
 await page.locator('.set-tabs').getByRole('button',{name:'Resources',exact:true}).click();await expect(page.getByRole('textbox',{name:'Name',exact:true})).toHaveValue('Fixture resource title');
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);await page.reload({waitUntil:'networkidle'});
 await page.locator('.set-tabs').getByRole('button',{name:messages.Resources,exact:true}).click();await expect(page.getByRole('textbox',{name:messages.Name,exact:true})).toHaveValue('Fixture resource title');
 await verify(page,info,'translated-add-resource');
});
