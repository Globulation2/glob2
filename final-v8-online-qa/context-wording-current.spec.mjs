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
test('Bulgarian owner uploads a cover through real API and music phase uses Build in player fixture',async({page,baseURL},info)=>{
 const seed=await(await page.request.get('/__seed')).json();
 // Fresh seeded registered account: prior provisional attempts exhausted Kestrel's six-per-day creation quota.
 await page.context().addCookies([{name:'glob2_session',value:seed.adminSession,url:baseURL}]);
 const mine=await(await page.request.get('/api/v1/music?mine=1')).json();
 for(const release of mine.items??[])if(release.metadata.title==='Fixture music title'&&release.status==='draft'){const deleted=await page.request.delete('/api/v1/music/'+release.id,{headers:{Origin:baseURL}});expect(deleted.ok()).toBe(true)}
 await page.goto('/music/new');
 await page.getByLabel(bg.Title,{exact:true}).fill('Fixture music title');
 await page.getByLabel(bg.Artist,{exact:true}).fill('Fixture composer');
 await page.getByLabel(bg['Credits & sources'],{exact:true}).fill('Synthetic fixture');
 await page.getByLabel(bg[' I can share these recordings and artwork under the selected licence.'],{exact:true}).check();
 await page.getByRole('button',{name:bg['Continue to uploads'],exact:true}).click();
 const cover=page.getByLabel(bg.Cover,{exact:true});await expect(cover).toBeVisible();
 const uploaded=page.waitForResponse(r=>r.request().method()==='PUT'&&new URL(r.url()).pathname.endsWith('/uploads/cover'));
 await cover.setInputFiles({name:'cover.png',mimeType:'image/png',buffer:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=','base64')});
 const response=await uploaded;expect(response.ok()).toBe(true);
 await expect(page.locator('label').filter({has:page.locator('input[type="file"][accept="image/*"]')})).toContainText(bg['✓ uploaded']);
 await expect(page.getByLabel(bg.Build,{exact:true})).toBeVisible();
 await expect(page.getByRole('status')).toHaveText(bg.Saved);
 await capture(page,info,'real-owner-cover-upload-bg');
 const cleanup=await page.request.delete('/api/v1/music/'+new URL(page.url()).pathname.split('/').at(-1),{headers:{Origin:baseURL}});expect(cleanup.ok()).toBe(true);
 // Shape-matched release fixture: the browser renders the production player;
 // no paid generation, conversion, audio playback, or published-data mutation.
 const release={id,ownerId:draftId,metadata:{title:'Fixture published title',artist:'Fixture composer',description:'Authored fixture',license:'CC0-1.0',credits:'Synthetic',sources:[],tags:[],aiGenerated:false},status:'published',createdAt:'2026-10-01T12:00:00Z',frames:480000,tracks:['calm','building','combat'].map(mood=>({mood,url:'/fixture-'+mood+'.opus',sha256:'a'.repeat(64),waveform:[0.2,0.6,0.4]})),warnings:[],inspection:null,uploaded:[],coverUrl:null,likes:0,downloads:0,liked:false,hidden:false,error:null};
 await page.route('**/api/v1/music/'+id,r=>r.fulfill({json:release}));
 await page.goto('/music/'+id);
 const crossfade=bg['Crossfade to {value0}'].replace('{value0}',bg.Build);
 const button=page.getByRole('button',{name:crossfade,exact:true});
 await expect(button.locator('strong')).toHaveText(bg.Build);
 await button.click();await expect(button).toHaveAttribute('aria-pressed','true');
 await expect(page.getByRole('img',{name:bg['{value0} waveform'].replace('{value0}',bg.Build),exact:true})).toBeVisible();
 const labels=await page.locator('.music-moods button').evaluateAll(buttons=>buttons.map(button=>{const bounds=button.getBoundingClientRect(),label=button.querySelector('strong'),range=document.createRange();range.selectNodeContents(label);return {text:label.textContent,button:{left:bounds.left,right:bounds.right,width:bounds.width},textRects:[...range.getClientRects()].map(r=>({left:r.left,right:r.right,width:r.width})),fits:[...range.getClientRects()].every(r=>r.left>=bounds.left-1&&r.right<=bounds.right+1)}}));
 await info.attach('mood-label-bounds',{body:Buffer.from(JSON.stringify(labels,null,2)),contentType:'application/json'});
 await capture(page,info,'fixture-music-phase-bg');
 expect(labels.every(label=>label.fits)).toBe(true);
});
test('Terrain Studio preview distinguishes infinite stock from harvested crop',async({page},info)=>{
 // Existing terrain-studio E2E contract with an infinite-yield resource.
 // Provider workflow is fixture-only; no request is submitted for generation.
 await page.route('**/api/v1/instance',async r=>{const response=await r.fetch();const instance=await response.json();await r.fulfill({json:{...instance,features:[...(instance.features??[]),'terrain-studio']}})});
 const pack={schemaVersion:1,setId:id,versionId:draftId,title:'Fixture renewable resource',description:'Authored fixture',tags:[],license:'CC-BY-4.0',credits:[{author:'Fixture',license:'CC-BY-4.0'}],terrains:[],resources:[{key:'s:trees',properties:{primaryMaterial:'wood',growthRate:65536,spreadRate:65536,farmable:true},yields:{wood:{capacity:5,initial:1,consumption:'infinite'}},presentation:{name:'Fixture trees',sprite:'data/gfx/ressource',minimap:[70,110,60],levels:[{stock:0,variants:[{frame:10,weight:1}]}]}}],assets:{schemaVersion:1,sheets:[],terrains:{},credits:[]}};
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
 await page.locator('.ts-entries').getByText(new RegExp(bg['stock remains available after harvesting'])).scrollIntoViewIfNeeded();
 await capture(page,info,'fixture-infinite-stock-bg');
});
