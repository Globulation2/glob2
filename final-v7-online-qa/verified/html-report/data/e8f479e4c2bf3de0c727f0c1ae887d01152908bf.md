# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: ../v5-final-online-qa/context-wording.spec.mjs >> Terrain Studio preview distinguishes infinite stock from harvested crop
- Location: artifacts/bulgarian/v5-final-online-qa/context-wording.spec.mjs:52:1

# Error details

```
Error: expect(locator).toContainText(expected) failed

Locator: locator('.ts-entries')
Expected substring: "Wood"
Received string:    "Fixture treesОсновен материалwoodТемп на растеж1.00× пшеницаСкорост на разпространение0.33× пшеницаМоже да се отглеждаДаwood: започва от 1, задържа 5; запасите остават налични след събиране"
Timeout: 20000ms

Call log:
  - Expect "toContainText" locator('.ts-entries') with timeout 20000ms
  - waiting for locator('.ts-entries')
    42 × locator resolved to <div class="ts-entries">…</div>
       - unexpected value "Fixture treesОсновен материалwoodТемп на растеж1.00× пшеницаСкорост на разпространение0.33× пшеницаМоже да се отглеждаДаwood: започва от 1, задържа 5; запасите остават налични след събиране"

```

```yaml
- article:
  - heading "Fixture trees" [level=3]
  - term: Основен материал
  - definition: wood
  - term: Темп на растеж
  - definition: 1.00× пшеница
  - term: Скорост на разпространение
  - definition: 0.33× пшеница
  - term: Може да се отглежда
  - definition: Да
  - list:
    - listitem:
      - code: wood
      - text: ": започва от 1, задържа 5; запасите остават налични след събиране"
```

# Test source

```ts
  1  | import { readFileSync } from 'node:fs';
  2  | import { test, expect } from '../../../platform/node_modules/@playwright/test/index.mjs';
  3  | const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
  4  | const id='11111111-1111-4111-8111-111111111111', draftId='22222222-2222-4222-8222-222222222222';
  5  | test.beforeEach(async({page,request,baseURL})=>{
  6  |  const seed=await(await request.get('/__seed')).json();
  7  |  await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.userSession,url:baseURL}]);
  8  | });
  9  | async function capture(page,info,name){
  10 |  await page.evaluate(()=>document.fonts.ready);
  11 |  await expect(page.locator('html')).toHaveAttribute('lang','bg');
  12 |  expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);
  13 |  expect(await page.evaluate(()=>[...document.fonts].some(f=>f.family.includes('Glob2 Bulgarian')&&f.status==='loaded'))).toBe(true);
  14 |  await info.attach(name,{body:await page.screenshot({fullPage:true}),contentType:'image/png'});
  15 | }
  16 | test('Bulgarian owner uploads a cover through real API and music phase uses Build in player fixture',async({page,baseURL},info)=>{
  17 |  const seed=await(await page.request.get('/__seed')).json();
  18 |  // Fresh seeded registered account: prior provisional attempts exhausted Kestrel's six-per-day creation quota.
  19 |  await page.context().addCookies([{name:'glob2_session',value:seed.adminSession,url:baseURL}]);
  20 |  const mine=await(await page.request.get('/api/v1/music?mine=1')).json();
  21 |  for(const release of mine.items??[])if(release.metadata.title==='Fixture music title'&&release.status==='draft'){const deleted=await page.request.delete('/api/v1/music/'+release.id,{headers:{Origin:baseURL}});expect(deleted.ok()).toBe(true)}
  22 |  await page.goto('/music/new');
  23 |  await page.getByLabel(bg.Title,{exact:true}).fill('Fixture music title');
  24 |  await page.getByLabel(bg.Artist,{exact:true}).fill('Fixture composer');
  25 |  await page.getByLabel(bg['Credits & sources'],{exact:true}).fill('Synthetic fixture');
  26 |  await page.getByLabel(bg[' I can share these recordings and artwork under the selected licence.'],{exact:true}).check();
  27 |  await page.getByRole('button',{name:bg['Continue to uploads'],exact:true}).click();
  28 |  const cover=page.getByLabel(bg.Cover,{exact:true});await expect(cover).toBeVisible();
  29 |  const uploaded=page.waitForResponse(r=>r.request().method()==='PUT'&&new URL(r.url()).pathname.endsWith('/uploads/cover'));
  30 |  await cover.setInputFiles({name:'cover.png',mimeType:'image/png',buffer:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=','base64')});
  31 |  const response=await uploaded;expect(response.ok()).toBe(true);
  32 |  await expect(page.locator('label').filter({has:page.locator('input[type="file"][accept="image/*"]')})).toContainText(bg['✓ uploaded']);
  33 |  await expect(page.getByLabel(bg.Build,{exact:true})).toBeVisible();
  34 |  await expect(page.getByRole('status')).toHaveText(bg.Saved);
  35 |  await capture(page,info,'real-owner-cover-upload-bg');
  36 |  const cleanup=await page.request.delete('/api/v1/music/'+new URL(page.url()).pathname.split('/').at(-1),{headers:{Origin:baseURL}});expect(cleanup.ok()).toBe(true);
  37 |  // Shape-matched release fixture: the browser renders the production player;
  38 |  // no paid generation, conversion, audio playback, or published-data mutation.
  39 |  const release={id,ownerId:draftId,metadata:{title:'Fixture published title',artist:'Fixture composer',description:'Authored fixture',license:'CC0-1.0',credits:'Synthetic',sources:[],tags:[],aiGenerated:false},status:'published',createdAt:'2026-10-01T12:00:00Z',frames:480000,tracks:['calm','building','combat'].map(mood=>({mood,url:'/fixture-'+mood+'.opus',sha256:'a'.repeat(64),waveform:[0.2,0.6,0.4]})),warnings:[],inspection:null,uploaded:[],coverUrl:null,likes:0,downloads:0,liked:false,hidden:false,error:null};
  40 |  await page.route('**/api/v1/music/'+id,r=>r.fulfill({json:release}));
  41 |  await page.goto('/music/'+id);
  42 |  const crossfade=bg['Crossfade to {value0}'].replace('{value0}',bg.Build);
  43 |  const button=page.getByRole('button',{name:crossfade,exact:true});
  44 |  await expect(button.locator('strong')).toHaveText(bg.Build);
  45 |  await button.click();await expect(button).toHaveAttribute('aria-pressed','true');
  46 |  await expect(page.getByRole('img',{name:bg['{value0} waveform'].replace('{value0}',bg.Build),exact:true})).toBeVisible();
  47 |  const labels=await page.locator('.music-moods button').evaluateAll(buttons=>buttons.map(button=>{const bounds=button.getBoundingClientRect(),label=button.querySelector('strong'),range=document.createRange();range.selectNodeContents(label);return {text:label.textContent,button:{left:bounds.left,right:bounds.right,width:bounds.width},textRects:[...range.getClientRects()].map(r=>({left:r.left,right:r.right,width:r.width})),fits:[...range.getClientRects()].every(r=>r.left>=bounds.left-1&&r.right<=bounds.right+1)}}));
  48 |  await info.attach('mood-label-bounds',{body:Buffer.from(JSON.stringify(labels,null,2)),contentType:'application/json'});
  49 |  await capture(page,info,'fixture-music-phase-bg');
  50 |  expect(labels.every(label=>label.fits)).toBe(true);
  51 | });
  52 | test('Terrain Studio preview distinguishes infinite stock from harvested crop',async({page},info)=>{
  53 |  // Existing terrain-studio E2E contract with an infinite-yield resource.
  54 |  // Provider workflow is fixture-only; no request is submitted for generation.
  55 |  await page.route('**/api/v1/instance',async r=>{const response=await r.fetch();const instance=await response.json();await r.fulfill({json:{...instance,features:[...(instance.features??[]),'terrain-studio']}})});
  56 |  const pack={schemaVersion:1,setId:id,versionId:draftId,title:'Fixture renewable resource',description:'Authored fixture',tags:[],license:'CC-BY-4.0',credits:[{author:'Fixture',license:'CC-BY-4.0'}],terrains:[],resources:[{key:'s:trees',properties:{primaryMaterial:'wood',growthRate:65536,spreadRate:65536,farmable:true},yields:{wood:{capacity:5,initial:1,consumption:'infinite'}},presentation:{name:'Fixture trees',sprite:'data/gfx/ressource',minimap:[70,110,60],levels:[{stock:0,variants:[{frame:10,weight:1}]}]}}],assets:{schemaVersion:1,sheets:[],terrains:{},credits:[]}};
  57 |  await page.addInitScript(()=>{class Events{constructor(){queueMicrotask(()=>this.onopen?.())}close(){}addEventListener(){}}Object.defineProperty(globalThis,'EventSource',{value:Events})});
  58 |  await page.route('**/api/v1/terrain-studio/**',r=>{
  59 |   const path=new URL(r.request().url()).pathname;
  60 |   if(path.endsWith('/account'))return r.fulfill({json:{enabled:true,available:3,reserved:0,packs:[]}});
  61 |   return r.fulfill({json:{id,title:pack.title,draftId,cursor:'0',references:[],revisions:[],messages:[],requests:[]}});
  62 |  });
  63 |  await page.route('**/api/v1/set-drafts/'+draftId,r=>r.fulfill({json:{id:draftId,revision:1,package:pack,publishedVersionId:null,validation:{status:'valid'}}}));
  64 |  await page.goto('/terrain-studio/'+id);
  65 |  await expect(page.locator('.ts-entries')).toBeAttached();
  66 |  const preview=page.getByRole('tab',{name:new RegExp('^'+bg.Preview)}).first();
  67 |  if(await preview.isVisible())await preview.click();
  68 |  await expect(page.locator('.ts-entries')).toBeVisible();
  69 |  await expect(page.locator('.studio-revisions')).toContainText(bg['Current saved draft']);
  70 |  await expect(page.locator('.studio-revisions')).not.toContainText('current saved draft');
  71 |  await expect(page.locator('.ts-entries')).toContainText(bg['stock remains available after harvesting']);
> 72 |  await expect(page.locator('.ts-entries')).toContainText('Wood');
     |                                            ^ Error: expect(locator).toContainText(expected) failed
  73 |  await page.locator('.ts-entries').getByText(new RegExp(bg['stock remains available after harvesting'])).scrollIntoViewIfNeeded();
  74 |  await capture(page,info,'fixture-infinite-stock-bg');
  75 | });
  76 | 
```