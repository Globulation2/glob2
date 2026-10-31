# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: translated-controls.spec.mjs >> map visibility labels translate while protocol values remain public unlisted private
- Location: artifacts/bulgarian/final-online-qa/translated-controls.spec.mjs:41:1

# Error details

```
Error: expect(locator).toHaveText(expected) failed

Locator: getByLabel('Видимост', { exact: true }).locator('option')
Timeout: 5000ms
- Expected  - 5
+ Received  + 1

- Array [
-   "Публично — показва се в списъците за всички",
-   "Само чрез връзка — достъпно по връзка",
-   "Частно — само вие",
- ]
+ Array []

Call log:
  - Expect "toHaveText" getByLabel('Видимост', { exact: true }).locator('option') with timeout 5000ms
  - waiting for getByLabel('Видимост', { exact: true }).locator('option')
    13 × locator resolved to 0 elements

```

# Page snapshot

```yaml
- generic [ref=e3]:
  - link "Към съдържанието" [ref=e4] [cursor=pointer]:
    - /url: "#main"
  - complementary [ref=e5]:
    - link "Glob2 Online (test), начало" [ref=e7] [cursor=pointer]:
      - /url: /
    - link "Игра в браузър" [ref=e8] [cursor=pointer]:
      - /url: /play/
    - navigation "Главно" [ref=e11]:
      - generic [ref=e12]:
        - link "Начало" [ref=e13] [cursor=pointer]:
          - /url: /
        - link "Класация" [ref=e14] [cursor=pointer]:
          - /url: /leaderboard
        - link "Играчи" [ref=e20] [cursor=pointer]:
          - /url: /players
        - link "Мачове" [ref=e27] [cursor=pointer]:
          - /url: /matches
      - generic [ref=e34]:
        - link "Карти" [ref=e35] [cursor=pointer]:
          - /url: /maps
        - link "Терен и ресурси" [ref=e39] [cursor=pointer]:
          - /url: /sets
        - link "Библиотека с ИИ" [ref=e46] [cursor=pointer]:
          - /url: /ais
        - link "Сгради" [ref=e52] [cursor=pointer]:
          - /url: /buildings
        - link "Музика" [ref=e56] [cursor=pointer]:
          - /url: /music
    - generic [ref=e62]:
      - generic [ref=e63]:
        - group [ref=e64]:
          - generic "За нас и помощ" [ref=e65]
        - 'button "Тема: същата като на това устройство. Промяна на темата" [ref=e66] [cursor=pointer]'
        - generic [ref=e71]:
          - text: Език
          - combobox "Език" [ref=e72]:
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
            - option "Български" [selected]
      - generic [ref=e73]:
        - link "Kestrel" [ref=e74] [cursor=pointer]:
          - /url: /account
        - button "Изход" [ref=e75] [cursor=pointer]
  - button "Отваряне на навигацията" [ref=e76] [cursor=pointer]
  - main [ref=e80]:
    - generic [ref=e82]:
      - generic [ref=e84]:
        - heading "Качване на карта" [level=1] [ref=e85]
        - paragraph [ref=e86]: Споделете карта, която сте създали в редактора или с генератор.
      - generic [ref=e87]:
        - generic [ref=e88]:
          - text: Файл с карта (.map или .map.gz, от играта или нейния редактор на карти)
          - button "Файл с карта (.map или .map.gz, от играта или нейния редактор на карти)" [ref=e89]
        - generic [ref=e90]:
          - text: Заглавие
          - textbox "Заглавие" [ref=e91]
        - generic [ref=e92]:
          - text: Описание
          - textbox "Описание" [ref=e93]
        - generic [ref=e94]:
          - text: Видимост
          - combobox "Видимост" [ref=e95]:
            - option "Публично — показва се в списъците за всички"
            - option "Само чрез връзка — достъпно по връзка" [selected]
            - option "Частно — само вие"
        - generic [ref=e96]:
          - text: Направено
          - combobox "Направено" [ref=e97]:
            - option "В редактора на карти" [selected]
            - option "Чрез генератор на карти"
        - button "Качване" [ref=e98] [cursor=pointer]
        - status [ref=e99]
        - paragraph [ref=e100]: Сървърът първо зарежда файла чрез играта, за да го провери; страницата на картата се създава само ако зареждането успее. Картите само чрез връзка са достъпни единствено с връзката си; можете да направите картата публична по-късно.
```

# Test source

```ts
  1  | import { readFileSync } from 'node:fs';
  2  | import { test, expect } from '../../../platform/node_modules/@playwright/test/index.mjs';
  3  | const messages=JSON.parse(readFileSync(new URL('../../../platform/packages/i18n/locales/bg.json',import.meta.url),'utf8'));
  4  | const tags=['Balanced','Rush','Defensive','Economy','Expansion','Experimental','Beginner-friendly'];
  5  | const id='11111111-1111-4111-8111-111111111111';
  6  | const createdAt='2026-10-01T12:00:00Z';
  7  | const version={id,hash:'a'.repeat(64),label:'1.2.0',notes:'',profile:2,createdAt,downloads:186,downloadUrl:'/api/v1/ais/'+id+'/versions/'+id+'/file',validations:[]};
  8  | const ai={id,name:'Patient Gardener',description:'A colony builder with a steady food supply.',tags:tags.slice(0,5),visibility:'public',hidden:false,owner:{id,displayName:'Maple'},createdAt,updatedAt:createdAt,likes:42,downloads:231,liked:false,favourited:false,latestVersion:version};
  9  | 
  10 | test.beforeEach(async({page,request,baseURL})=>{
  11 |  const seed=await(await request.get('/__seed')).json();
  12 |  await page.context().addCookies([{name:'glob2_locale',value:'bg',url:baseURL},{name:'glob2_session',value:seed.userSession,url:baseURL}]);
  13 | });
  14 | async function verify(page,info,name){
  15 |  await page.evaluate(()=>document.fonts.ready);
  16 |  await expect(page.locator('html')).toHaveAttribute('lang','bg');
  17 |  expect(await page.evaluate(()=>[...document.fonts].some(f=>f.family.includes('Glob2 Bulgarian')&&f.status==='loaded'))).toBe(true);
  18 |  expect(await page.evaluate(()=>document.documentElement.scrollWidth-innerWidth)).toBeLessThanOrEqual(1);
  19 |  await info.attach(name,{body:await page.screenshot({fullPage:true}),contentType:'image/png'});
  20 | }
  21 | 
  22 | test('AI filter, card, detail and publish labels translate while canonical tags stay stable',async({page},info)=>{
  23 |  await page.route('**/api/v1/ais*',r=>new URL(r.request().url()).pathname==='/api/v1/ais'?r.fulfill({json:{items:[ai]}}):r.continue());
  24 |  await page.route('**/api/v1/ais/'+id,r=>r.fulfill({json:{ai,versions:[version],viewer:{owner:false,moderator:false}}}));
  25 |  await page.goto('/ais',{waitUntil:'networkidle'});
  26 |  await expect(page.locator('.ai-tags[role="group"] button')).toHaveText(tags.map(t=>messages[t]));
  27 |  await expect(page.locator('.library-card .ai-tags .badge')).toHaveText(ai.tags.map(t=>messages[t]));
  28 |  const filtered=page.waitForRequest(r=>new URL(r.url()).pathname==='/api/v1/ais'&&new URL(r.url()).searchParams.get('tags')==='Rush');
  29 |  await page.getByRole('group',{name:messages['AI tags']}).getByRole('button',{name:messages.Rush,exact:true}).click();
  30 |  await filtered;
  31 |  await verify(page,info,'translated-ai-filter-card');
  32 |  await page.goto('/ais/'+id,{waitUntil:'networkidle'});
  33 |  await expect(page.locator('h1')).toHaveText(ai.name);
  34 |  await expect(page.locator('.ai-detail-grid .ai-tags .badge')).toHaveText(ai.tags.map(t=>messages[t]));
  35 |  await verify(page,info,'translated-ai-detail');
  36 |  await page.goto('/ais/new',{waitUntil:'networkidle'});
  37 |  await expect(page.locator('.ai-tags[role="group"] button')).toHaveText(tags.map(t=>messages[t]));
  38 |  await verify(page,info,'translated-ai-publish');
  39 | });
  40 | 
  41 | test('map visibility labels translate while protocol values remain public unlisted private',async({page},info)=>{
  42 |  await page.goto('/maps/new',{waitUntil:'networkidle'});
  43 |  const select=page.getByLabel(messages.Visibility,{exact:true});
> 44 |  await expect(select.locator('option')).toHaveText(['Public — listed for everyone','Unlisted — accessible by link','Private — only you'].map(k=>messages[k]));
     |                                         ^ Error: expect(locator).toHaveText(expected) failed
  45 |  await expect(select).toHaveValue('unlisted');
  46 |  await select.selectOption('public');await expect(select).toHaveValue('public');
  47 |  await verify(page,info,'translated-map-visibility');
  48 | });
  49 | 
  50 | test('music author labels use Artist and attribution Credits and sources',async({page},info)=>{
  51 |  await page.goto('/music/new',{waitUntil:'networkidle'});
  52 |  for(const key of ['Title','Artist','Description','Credits & sources'])await expect(page.getByLabel(messages[key],{exact:true})).toBeVisible();
  53 |  await expect(page.locator('.music-create form > label').filter({hasText:/^(Title|Artist|Description|Credits)$/})).toHaveCount(0);
  54 |  await verify(page,info,'translated-music-attribution');
  55 | });
  56 | 
```