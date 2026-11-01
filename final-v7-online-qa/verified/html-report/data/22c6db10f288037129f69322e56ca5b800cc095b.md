# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: new-domains.spec.mjs >> new skin model and material captions use Bulgarian catalog
- Location: artifacts/bulgarian/final-v7-online-qa/new-domains.spec.mjs:16:1

# Error details

```
Error: expect(locator).toBeEnabled() failed

Locator: getByLabel('Име на облика', { exact: true })
Expected: enabled
Timeout: 20000ms
Error: element(s) not found

Call log:
  - Expect "toBeEnabled" getByLabel('Име на облика', { exact: true }) with timeout 20000ms
  - waiting for getByLabel('Име на облика', { exact: true })

```

```yaml
- link "Към съдържанието":
  - /url: "#main"
- complementary:
  - link "Glob2 Online (test), начало":
    - /url: /
  - button "Свиване на страничната лента" [expanded]
  - link "Игра в браузър":
    - /url: /play/
  - navigation "Главно":
    - text: Игра
    - link "Начало":
      - /url: /
    - link "Класация":
      - /url: /leaderboard
    - link "Играчи":
      - /url: /players
    - link "Мачове":
      - /url: /matches
    - text: Създаване
    - link "Карти":
      - /url: /maps
    - link "Терен и ресурси":
      - /url: /sets
    - link "Библиотека с ИИ":
      - /url: /ais
    - link "Сгради":
      - /url: /buildings
    - link "Музика":
      - /url: /music
  - group: За нас и помощ
  - 'button "Тема: същата като на това устройство. Промяна на темата"'
  - text: Език
  - combobox "Език":
    - option "العربية"
    - option "Português brasileiro"
    - option "Català"
    - option "Čeština"
    - option "Deutsch"
    - option "Dansk"
    - option "English"
    - option "Español"
    - option "Esperanto"
    - option "Euskara"
    - option "فارسی"
    - option "Français"
    - option "Ελληνικά"
    - option "Magyar"
    - option "Italiano"
    - option "Nederlands"
    - option "Polski"
    - option "Português europeu"
    - option "Română"
    - option "Русский"
    - option "Slovenščina"
    - option "Slovenčina"
    - option "Српски"
    - option "Suomi"
    - option "Svenska"
    - option "Türkçe"
    - option "繁體中文"
    - option "简体中文"
    - option "Українська"
    - option "Bahasa Indonesia"
    - option "Tiếng Việt"
    - option "日本語"
    - option "한국어"
    - option "हिन्दी"
    - option "اردو"
    - option "Kiswahili"
    - option "বাংলা"
    - option "Norsk bokmål"
    - option "Bahasa Melayu"
    - option "Български" [selected]
  - link "Вход":
    - /url: /signin
- main:
  - heading "Не е налично" [level=1]
```

# Test source

```ts
  1  | import {test,expect} from '../../../platform/node_modules/@playwright/test/index.mjs';
  2  | import {readFileSync,mkdirSync} from 'node:fs';
  3  | const bg=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
  4  | async function capture(page,info,name){
  5  |  await page.evaluate(()=>document.fonts.ready);
  6  |  expect(await page.evaluate(()=>[...document.fonts].some(f=>f.family.includes('Glob2 Bulgarian')&&f.status==='loaded'))).toBe(true);
  7  |  expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth),name).toBeLessThanOrEqual(1);
  8  |  await expect(page.locator('html')).toHaveAttribute('lang','bg');
  9  |  const out=new URL('./verified/screenshots/',import.meta.url);mkdirSync(out,{recursive:true});
  10 |  await page.screenshot({path:new URL(info.project.name+'-'+name+'.png',out).pathname,fullPage:true});
  11 |  await info.attach(name,{body:await page.screenshot({fullPage:true}),contentType:'image/png'});
  12 |  const keys=JSON.parse(readFileSync(new URL('./new-source-keys.json',import.meta.url),'utf8'));
  13 |  const receipt=await page.evaluate(({keys,bg})=>{const text=document.body.innerText,attributes=[...document.querySelectorAll('[aria-label],[title]')].flatMap(e=>[e.getAttribute('aria-label'),e.getAttribute('title')]).filter(Boolean);return {url:location.href,viewport:{width:innerWidth,height:innerHeight},overflow:document.documentElement.scrollWidth-innerWidth,matchedNewSources:keys.filter(key=>text.includes(bg[key])||attributes.includes(bg[key]))}}, {keys,bg});
  14 |  await info.attach(name+'-receipt',{body:Buffer.from(JSON.stringify(receipt,null,2)),contentType:'application/json'});
  15 | }
  16 | test('new skin model and material captions use Bulgarian catalog',async({page,baseURL},info)=>{
  17 |  await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);
  18 |  await page.goto('/skins',{waitUntil:'networkidle'});
> 19 |  await expect(page.getByLabel(bg['Skin name'],{exact:true})).toBeEnabled();
     |                                                              ^ Error: expect(locator).toBeEnabled() failed
  20 |  await expect(page.locator('.sidebar-auth')).toHaveAttribute('aria-label',bg['Sign in']);
  21 |  if(info.project.name.endsWith('phone')){const box=await page.locator('.sidebar-auth').boundingBox();expect(box.width).toBeGreaterThanOrEqual(44);expect(box.height).toBeGreaterThanOrEqual(44);}
  22 |  for(const key of ['Worker','Warrior','Explorer','Swarm'])await expect(page.getByRole('button',{name:bg[key],exact:true})).toBeVisible();
  23 |  await page.getByRole('button',{name:bg.Material,exact:true}).click();
  24 |  for(const key of ['Classic glossy','Matte','Riveted metal','Hairy','Woven fabric'])await expect(page.getByRole('radio',{name:bg[key],exact:true})).toBeAttached();
  25 |  await capture(page,info,'new-skin-materials');
  26 | });
  27 | test('new administrative tabs and finance filters render Bulgarian on real seeded API',async({page,request,baseURL},info)=>{
  28 |  const seed=await(await request.get('/__seed')).json();
  29 |  await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.adminSession,url:baseURL}]);
  30 |  for(const route of ['finances','accounts','audit']){
  31 |   await page.goto('/admin/'+route,{waitUntil:'networkidle'});
  32 |   await expect(page.getByRole('heading',{name:bg.Administration,exact:true})).toBeVisible();
  33 |   await expect(page.locator('.sidebar-auth')).toHaveAttribute('aria-label',bg['Sign out']);
  34 |   if(info.project.name.endsWith('phone')){const box=await page.locator('.sidebar-auth').boundingBox();expect(box.width).toBeGreaterThanOrEqual(44);expect(box.height).toBeGreaterThanOrEqual(44);}
  35 |   for(const key of ['Finances','Accounts','Audit'])await expect(page.getByRole('link',{name:bg[key],exact:true})).toBeVisible();
  36 |   await capture(page,info,'new-admin-'+route);
  37 |  }
  38 | });
  39 | test('all new music listening guidance is localized in actual published player UI fixture',async({page,baseURL},info)=>{
  40 |  const id='11111111-1111-4111-8111-111111111111';
  41 |  const source=['Listen for the musical structure and development.','Check that all three moods play correctly.','Listen for unexpected jumps in volume between moods.','Listen for a click, gap, or abrupt change when the loop repeats.','Listen for short phrases that repeat too mechanically.','Switch moods and listen for changes in timing or harmony.','Check that building and combat feel distinct from calm.','Listen for hiss or scratchiness in quiet passages.','Listen for muddy bass or harsh high frequencies.','Check that calm passages remain audible alongside game sound.','Listen for notes that cut off unexpectedly.'];
  42 |  const labels=['score','format','loudness','seam','repetition','alignment','contrast','noise','balance','audibility','dropout'];
  43 |  const release={id,ownerId:'22222222-2222-4222-8222-222222222222',metadata:{title:'Fixture listening guidance',artist:'Fixture composer',description:'Authored fixture',license:'CC0-1.0',credits:'Synthetic',sources:[],tags:[],aiGenerated:false},status:'published',createdAt:'2026-10-01T12:00:00Z',frames:480000,tracks:['calm','building','combat'].map(mood=>({mood,url:'/fixture-'+mood+'.opus',sha256:'a'.repeat(64),waveform:[0.2,0.6,0.4]})),warnings:[],inspection:null,validation:labels.map(label=>({id:label,label,status:'warn',attempt:1,measures:[]})),uploaded:[],coverUrl:null,likes:0,downloads:0,liked:false,hidden:false,error:null};
  44 |  await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL}]);
  45 |  await page.route('**/api/v1/music/'+id,r=>r.fulfill({json:release}));
  46 |  await page.goto('/music/'+id,{waitUntil:'networkidle'});
  47 |  for(const key of source)await expect(page.getByText(bg[key],{exact:true})).toBeVisible();
  48 |  await capture(page,info,'new-music-guidance-fixture');
  49 | });
  50 | 
```