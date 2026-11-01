import { readFileSync } from 'node:fs';
import { test, expect } from '../../../platform/node_modules/@playwright/test/index.mjs';
const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
const id='11111111-1111-4111-8111-111111111111', draftId='22222222-2222-4222-8222-222222222222';
test.beforeEach(async({page,request,baseURL})=>{
 const seed=await(await request.get('/__seed')).json();
 await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.userSession,url:baseURL}]);
});
async function capture(page,info,name){
 await page.evaluate(()=>document.fonts.ready);
 await expect(page.locator('html')).toHaveAttribute('lang','bg');
 expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);
 expect(await page.evaluate(()=>[...document.fonts].some(f=>f.family.includes('Glob2 Bulgarian')&&f.status==='loaded'))).toBe(true);
 await info.attach(name,{body:await page.screenshot({fullPage:true}),contentType:'image/png'});
}
test('Terrain Studio preview distinguishes infinite stock from harvested crop',async({page},info)=>{
 // Existing terrain-studio E2E contract with an infinite-yield resource.
 // Provider workflow is fixture-only; no request is submitted for generation.
 await page.route('**/api/v1/instance',async r=>{const response=await r.fetch();const instance=await response.json();await r.fulfill({json:{...instance,features:[...(instance.features??[]),'terrain-studio']}})});
 const pack={schemaVersion:1,setId:id,versionId:draftId,title:'Fixture renewable resource',description:'Authored fixture',tags:[],license:'CC-BY-4.0',credits:[{author:'Fixture',license:'CC-BY-4.0'}],terrains:[{key:'s:marsh',name:'Fixture marsh',base:'marsh',properties:{groundSpeedQ8:128,unknownHealthQ8:-256,constructor:'authored constructor'}}],resources:[{key:'s:trees',properties:{primaryMaterial:'wood',growthRate:65536,spreadRate:65536,farmable:true},yields:{wood:{capacity:5,initial:1,consumption:'infinite'}},presentation:{name:'Fixture trees',sprite:'data/gfx/ressource',minimap:[70,110,60],levels:[{stock:0,variants:[{frame:10,weight:1}]}]}}],assets:{schemaVersion:1,sheets:[],terrains:{},credits:[]}};
 await page.addInitScript(()=>{class Events{constructor(){queueMicrotask(()=>this.onopen?.())}close(){}addEventListener(){}}Object.defineProperty(globalThis,'EventSource',{value:Events})});
 await page.route('**/api/v1/terrain-studio/**',r=>{
  const path=new URL(r.request().url()).pathname;
  if(path.endsWith('/account'))return r.fulfill({json:{enabled:true,available:3,reserved:0,packs:[]}});
  return r.fulfill({json:{id,title:pack.title,draftId,cursor:'0',references:[],revisions:[],messages:[],requests:[]}});
 });
 await page.route('**/api/v1/set-drafts/'+draftId,r=>r.fulfill({json:{id:draftId,revision:1,package:pack,publishedVersionId:null,validation:{status:'valid'}}}));
 await page.goto('/terrain-studio/'+id);
 await expect(page.locator('.ts-entries')).toBeAttached();
 const preview=page.getByRole('tab',{name:new RegExp('^'+bg.Preview)}).first();
 if(await preview.isVisible())await preview.click();
 await expect(page.locator('.ts-entries')).toBeVisible();
 await expect(page.locator('.studio-revisions')).toContainText(bg['Current saved draft']);
 await expect(page.locator('.studio-revisions')).not.toContainText('current saved draft');
 await expect(page.locator('.ts-entries')).toContainText(bg['stock remains available after harvesting']);
 await expect(page.locator('.ts-entries')).toContainText('wood');
 await expect(page.locator('.ts-entries dt').getByText(bg['Ground movement'],{exact:true})).toBeVisible();
 for(const key of ['unknownHealthQ8','constructor'])await expect(page.locator('.ts-entries dt code').getByText(key,{exact:true})).toBeVisible();
 await page.locator('.ts-entries dt code').getByText('unknownHealthQ8',{exact:true}).scrollIntoViewIfNeeded();
 await capture(page,info,'fixture-terrain-raw-identifiers');
 await page.locator('.ts-entries').getByText(new RegExp(bg['stock remains available after harvesting'])).scrollIntoViewIfNeeded();
 await capture(page,info,'fixture-infinite-stock-bg');
});

test('published set inspector presents exact serialized field IDs as code',async({page},info)=>{
 const pack={terrains:[{key:'fixture:terrain',name:'Fixture authored terrain',base:'grass',properties:{groundSpeedQ8:256,unknownVelocityQ8:384,constructor:'authored constructor'}}],resources:[],assets:{terrains:{}}};
 await page.route('**/api/v1/sets/'+id+'/**',r=>r.fulfill({json:pack}));
 await page.route('**/api/v1/sets/'+id,r=>r.fulfill({json:{id,title:'Fixture published set',description:'Authored fixture',owner:{id:draftId,displayName:'Fixture author'},versions:[{id,hash:'a'.repeat(64),label:'1.0',notes:'',credits:[],license:'CC0-1.0',simVersion:'fixture',minVersionMinor:191,terrainCount:1,resourceCount:0,createdAt:'2026-10-01T12:00:00Z'}],tags:[],visibility:'public',hidden:false,likes:0,liked:false,downloads:0,createdAt:'2026-10-01T12:00:00Z',updatedAt:'2026-10-01T12:00:00Z'}}));
 // Existing bundled terrain tile is a fixture thumbnail, not a native generated preview.
 await page.route('**/api/v1/sets/'+id+'/versions/'+id+'/preview',r=>r.fulfill({contentType:'image/png',body:readFileSync(new URL('../../../data/gfx/terrain-grass0.png',import.meta.url))}));
 await page.goto('/sets/'+id);
 await page.getByRole('button',{name:bg['Inspect latest release'],exact:true}).click();
 const inspector=page.locator('details').filter({has:page.getByText(bg['Terrain']+' · Fixture authored terrain',{exact:true})}).first();
 await inspector.locator('summary').first().click();
 for(const key of ['groundSpeedQ8','unknownVelocityQ8','constructor'])await expect(inspector.locator('dt code').getByText(key,{exact:true})).toBeVisible();
 await expect(inspector).toContainText('256');await expect(inspector).toContainText('384');
 await inspector.locator('dt code').getByText('groundSpeedQ8',{exact:true}).scrollIntoViewIfNeeded();
 await capture(page,info,'fixture-set-readonly-identifiers');
});
