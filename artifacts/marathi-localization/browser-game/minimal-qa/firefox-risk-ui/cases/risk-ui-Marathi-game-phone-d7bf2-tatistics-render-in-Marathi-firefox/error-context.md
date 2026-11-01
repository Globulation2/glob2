# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: risk-ui.spec.js >> Marathi game phone >> threaded native catalog, selector and statistics render in Marathi
- Location: artifacts/marathi-localization/browser-game/risk-ui.spec.js:84:49

# Error details

```
Error: expect(received).toBeGreaterThan(expected)

Expected: > 25
Received:   1

Call Log:
- Timeout 120000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [ref=f1e1]:
  - generic [aria-hidden]:
    - status: खेळ सुरू करत आहे…
    - generic:
      - progressbar
      - generic: ४८ MB पैकी ४८
    - combobox
    - paragraph: पहिल्या भेटीत गेम एकदा डाउनलोड होतो. त्यानंतरच्या भेटी याच डिव्हाइसमधील कॉपीपासून सुरू होतात.
  - generic "Globulation 2" [active] [ref=f1e3]
```

# Test source

```ts
  34  |   }, {timeout: 180000}).toBe(true);
  35  | }
  36  | 
  37  | async function choosePublishedPopupOption(page, text) {
  38  |   let options;
  39  |   await expect.poll(async () => {
  40  |     options = await page.evaluate(() => Object.entries(glob2Diagnostics.snapshot().controls)
  41  |       .filter(([key]) => /^popup\/\d+$/.test(key))
  42  |       .sort(([a], [b]) => Number(a.split('/')[1]) - Number(b.split('/')[1]))
  43  |       .map(([, value]) => value.label));
  44  |     return options.includes(text);
  45  |   }, {message: `popup option "${text}" is not available`}).toBe(true);
  46  |   // The selector was opened by a real click and owns keyboard focus. Read its
  47  |   // actual choices rather than assuming locale indices or popup wheel bounds.
  48  |   const index = options.indexOf(text);
  49  |   const fromEnd = index > options.length / 2;
  50  |   const canvas = page.locator('#canvas');
  51  |   await canvas.press(fromEnd ? 'End' : 'Home', {delay: 40});
  52  |   const steps = fromEnd ? options.length - 1 - index : index;
  53  |   for (let step = 0; step < steps; ++step)
  54  |     await canvas.press(fromEnd ? 'ArrowUp' : 'ArrowDown', {delay: 40});
  55  |   await canvas.press('Enter', {delay: 40});
  56  | }
  57  | 
  58  | async function playerCategory(page, currentLanguage) {
  59  |   await clickMainMenu(page, 'settings');
  60  |   await screen(page, 'SettingsScreen');
  61  |   if ((await snapshot(page)).controls['nav.5']) await clickControl(page, 'nav.5');
  62  |   else {
  63  |     await clickControl(page, 'nav.current');
  64  |     await choosePublishedPopupOption(page, currentLanguage === 'mr' ? 'भाषा आणि खेळाडू' : 'Language & player');
  65  |   }
  66  |   await control(page, 'player.language');
  67  | }
  68  | 
  69  | async function chooseLanguage(page, languageName) {
  70  |   await clickControl(page, 'player.language');
  71  |   await choosePublishedPopupOption(page, languageName);
  72  |   await expect.poll(() => label(page, 'player.language')).toBe(languageName);
  73  |   await clickSettingsDone(page);
  74  |   await screen(page, 'MainMenuScreen');
  75  |   await expect.poll(async () => (await snapshot(page)).persisting).toBe(false);
  76  |   await expect.poll(async () => (await snapshot(page)).persistence).toBe('persisted');
  77  | }
  78  | 
  79  | for (const device of [
  80  |   {name: 'desktop', viewport: {width: 1280, height: 900}, hasTouch: false},
  81  |   {name: 'phone', viewport: {width: 390, height: 844}, hasTouch: true},
  82  | ]) test.describe(`Marathi game ${device.name}`, () => {
  83  |   test.use({viewport: device.viewport, hasTouch: device.hasTouch});
  84  |   for (const variant of ['serial', 'threaded']) test(`${variant} native catalog, selector and statistics render in Marathi`, async ({page, context, baseURL}, info) => {
  85  |     test.setTimeout(360000);
  86  |     const selected = {chromium: ['desktop','serial'], firefox: ['phone','threaded'], webkit: ['desktop','threaded']}[info.project.name];
  87  |     test.skip(device.name !== selected[0] || variant !== selected[1], 'Bounded current-risk refresh; prior full12 remains explicitly carried.');
  88  |     const evidence = path.join(__dirname, 'risk-ui', info.project.name, variant, device.name);
  89  |     fs.mkdirSync(evidence, {recursive: true});
  90  |     const errors = [];
  91  |     page.on('pageerror', error => errors.push(String(error)));
  92  |     // Each Playwright test has a fresh browser profile: only the online cookie
  93  |     // is seeded, with no saved native preferences or injected game state.
  94  |     await context.addCookies([{name: 'glob2_locale', value: 'mr', url: baseURL}]);
  95  |     const url = new URL(gameURL(), baseURL);
  96  |     url.searchParams.set('threads', variant);
  97  |     if (!url.searchParams.has('renderer')) url.searchParams.set('renderer', 'software');
  98  |     await page.goto(url.pathname + url.search);
  99  |     await initialMenu(page);
  100 |     expect((await snapshot(page)).executionMode).toBe(variant);
  101 |     await expect.poll(() => label(page, 'menu/1')).toBe('शिकवणी');
  102 |     expect((await snapshot(page)).assets.translations).toBe('ready');
  103 | 
  104 |     const registry = await page.evaluate(() => Glob2I18n.locales);
  105 |     expect(registry.length).toBe(39);
  106 |     expect(registry.map(entry => entry.code)).toEqual(expect.arrayContaining(['mr','ms']));
  107 |     // Read the actual packaged VFS bytes, independently of HTML's locale.
  108 |     const catalog = await readBrowserFile(page, '/data/texts.mr.txt');
  109 |     const font = await readBrowserFile(page, '/data/fonts/scripts/NotoSansDevanagari-Regular.ttf');
  110 |     const inventory = await readBrowserFile(page, '/data/texts.list.txt');
  111 |     expect(inventory.toString('utf8').split(/\r?\n/)).toContain('data/texts.mr.txt');
  112 |     expect(sha256(catalog)).toBe(sha256(fs.readFileSync(path.resolve(__dirname, '../../../data/texts.mr.txt'))));
  113 |     const fonts = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../../../data/fonts/scripts/manifest.json'), 'utf8'));
  114 |     expect(sha256(font)).toBe(fonts.fonts.find(entry => entry.name === 'NotoSansDevanagari').sha256);
  115 |     const records = {variant, device: device.name, browser: info.project.name,
  116 |       catalogSha256: sha256(catalog), devanagariFontSha256: sha256(font), main: await snapshot(page)};
  117 |     await page.screenshot({path: path.join(evidence, 'main.png')});
  118 | 
  119 |     await playerCategory(page, 'mr');
  120 |     await expect.poll(() => label(page, 'player.language')).toBe('मराठी');
  121 |     const languageBox = await controlBox(page, 'player.language', {fullyVisible: true});
  122 |     // Exclude the button border; verify actual canvas ink in the selected name.
  123 |     expect(await hasDarkText(page, {x: languageBox.x + 8, y: languageBox.y + 5,
  124 |       width: Math.max(1, languageBox.width - 35), height: Math.max(1, languageBox.height - 10)}, 180)).toBe(true);
  125 |     records.settings = await snapshot(page);
  126 |     await page.screenshot({path: path.join(evidence, 'settings.png')});
  127 |     await clickSettingsDone(page);
  128 |     await screen(page, 'MainMenuScreen');
  129 | 
  130 |     // Current shared change: narrow statistics Overview occupies its own row.
  131 |     await page.reload(); await initialMenu(page);
  132 |     await clickMainMenu(page, 'custom'); await clickControl(page, 'tab/2');
  133 |     await require(path.resolve(__dirname, '../../../browser/tests/main-menu')).clickCustomGameStart(page);
> 134 |     await expect.poll(async () => (await snapshot(page)).tick, {timeout:120000}).toBeGreaterThan(25);
      |                                                                                  ^ Error: expect(received).toBeGreaterThan(expected)
  135 |     if (device.hasTouch) {
  136 |       await page.touchscreen.tap(162,820);
  137 |       const toolsFrame=(await snapshot(page)).frames;
  138 |       await expect.poll(async () => (await snapshot(page)).frames).toBeGreaterThan(toolsFrame+2);
  139 |       await page.screenshot({path:path.join(evidence,'statistics-tools.png')});
  140 |       await page.touchscreen.tap(75,445);
  141 |     } else await page.mouse.click(1196,163);
  142 |     await control(page,'stats/topic/0');
  143 |     expect(await label(page,'stats/topic/0')).toBe('आढावा');
  144 |     expect(await label(page,'stats/topic/2')).toBe('अर्थव्यवस्था');
  145 |     const statsFrame=(await snapshot(page)).frames;
  146 |     await expect.poll(async () => (await snapshot(page)).frames).toBeGreaterThan(statsFrame+2);
  147 |     records.statisticsOverview=await snapshot(page);
  148 |     records.statisticsTopicBounds={};
  149 |     for(let index=0;index<5;++index)
  150 |       records.statisticsTopicBounds[index]=await controlBox(page,'stats/topic/'+index,{fullyVisible:true});
  151 |     await page.screenshot({path:path.join(evidence,'statistics-overview.png')});
  152 |     records.currentRegistry=registry;
  153 |     records.errors = errors;
  154 |     fs.writeFileSync(path.join(evidence, 'evidence.json'), JSON.stringify(records, null, 2) + '\n');
  155 |     expect(errors).toEqual([]);
  156 |   });
  157 | });
  158 | 
```