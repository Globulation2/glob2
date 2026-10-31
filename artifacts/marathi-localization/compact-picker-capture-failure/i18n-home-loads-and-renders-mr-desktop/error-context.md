# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: i18n.spec.ts >> home loads and renders mr
- Location: e2e/i18n.spec.ts:7:3

# Error details

```
Error: page.screenshot: Protocol error (Page.captureScreenshot): Unable to capture screenshot
Call log:
  - taking page screenshot
  - waiting for fonts to load...
  - fonts loaded

```

# Page snapshot

```yaml
- generic [ref=e3]:
  - link "सामग्रीवर जा" [ref=e4] [cursor=pointer]:
    - /url: "#main"
  - complementary [ref=e5]:
    - generic [ref=e6]:
      - link "Glob2 Online (test), मुख्यपृष्ठ" [ref=e7] [cursor=pointer]:
        - /url: /
      - button "साइडबार संकुचित करा" [expanded] [ref=e9] [cursor=pointer]
    - link "ब्राउझरमध्ये खेळा" [ref=e13] [cursor=pointer]:
      - /url: /play/
    - navigation "मुख्य" [ref=e17]:
      - generic [ref=e18]:
        - generic [ref=e19]: खेळा
        - link "मुख्यपृष्ठ" [ref=e20] [cursor=pointer]:
          - /url: /
        - link "लीडरबोर्ड" [ref=e22] [cursor=pointer]:
          - /url: /leaderboard
        - link "खेळाडू" [ref=e29] [cursor=pointer]:
          - /url: /players
        - link "सामने" [ref=e37] [cursor=pointer]:
          - /url: /matches
      - generic [ref=e45]:
        - generic [ref=e46]: तयार करा
        - link "नकाशे" [ref=e47] [cursor=pointer]:
          - /url: /maps
        - link "भूभाग आणि संसाधने" [ref=e52] [cursor=pointer]:
          - /url: /sets
        - link "एआय लायब्ररी" [ref=e60] [cursor=pointer]:
          - /url: /ais
        - link "इमारती" [ref=e67] [cursor=pointer]:
          - /url: /buildings
        - link "संगीत" [ref=e72] [cursor=pointer]:
          - /url: /music
    - generic [ref=e79]:
      - generic [ref=e80]:
        - group [ref=e81]:
          - generic "माहिती आणि मदत" [ref=e82]
        - 'button "थीम: या डिव्हाइसप्रमाणेच. थीम बदला" [ref=e83] [cursor=pointer]'
        - generic [ref=e88]:
          - generic [ref=e89]: भाषा
          - combobox "भाषा" [ref=e91]:
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
            - option "मराठी" [selected]
      - link "साइन इन करा" [ref=e93] [cursor=pointer]:
        - /url: /signin
  - main [ref=e94]:
    - generic [ref=e96]:
      - region [ref=e97]:
        - generic [ref=e101]:
          - heading "वसाहतीत आपले स्वागत आहे" [level=1] [ref=e102]
          - paragraph [ref=e103]: तुमची पुढची वसाहत येथून सुरू होते. तुमच्या ब्राउझरमध्ये खेळा किंवा तुमच्या मित्रांसोबत सामील व्हा.
          - generic [ref=e104]:
            - link "ब्राउझरमध्ये खेळा" [ref=e105] [cursor=pointer]:
              - /url: /play/
            - link "डाउनलोड करा" [ref=e108] [cursor=pointer]:
              - /url: https://github.com/Globulation2/glob2/releases
          - generic [ref=e111]:
            - generic [ref=e112]:
              - text: तुम्हाला इन्व्हाइट कोड मिळाला आहे का?
              - textbox "तुम्हाला इन्व्हाइट कोड मिळाला आहे का?" [ref=e113]:
                - /placeholder: कोड किंवा आमंत्रण लिंक
            - button "सामील व्हा" [ref=e114] [cursor=pointer]
            - paragraph [ref=e115]: मित्र त्यांच्या खोलीतून एक कोड किंवा लिंक शेअर करतात.
          - list [ref=e116]:
            - listitem [ref=e117]:
              - generic [ref=e119]: १
              - generic [ref=e120]: खेळाडू ऑनलाइन
            - listitem [ref=e121]:
              - generic [ref=e122]: १
              - generic [ref=e123]: सामना आता सुरू आहे
            - listitem [ref=e124]:
              - generic [ref=e125]: ३
              - generic [ref=e126]: आजचे सामने
          - paragraph [ref=e127]: रूम्स आणि कॅज्युअल गेम्ससाठी खात्याची आवश्यकता नाही; रँक मिळवण्यासाठी साइन इन करा.
      - generic [ref=e128]:
        - region [ref=e129]:
          - generic [ref=e130]:
            - heading "अलीकडील सामने" [level=2] [ref=e131]
            - link "सर्व सामने" [ref=e132] [cursor=pointer]:
              - /url: /matches
          - generic [ref=e133]:
            - link "Bradley vs Kestrel Even Ground · आज ०५:०९ PM 1 vs 1 ranked ११ मिनि तपासत आहे…" [ref=e134] [cursor=pointer]:
              - /url: /matches/4233abe3-cd83-4ab8-9136-b1a231b5b2d9
              - generic [ref=e138]:
                - generic [ref=e139]: Bradley vs Kestrel
                - generic [ref=e140]: Even Ground · आज ०५:०९ PM
              - generic [ref=e141]: 1 vs 1 ranked
              - generic [ref=e142]: ११ मिनि
              - generic [ref=e143]: तपासत आहे…
            - link "Ana_M vs Guest-4821 Marchland · काल खोली १७ मिनि ✓ सत्यापित" [ref=e145] [cursor=pointer]:
              - /url: /matches/7aa15883-e69f-42eb-901e-7dfd93f181f0
              - generic [ref=e149]:
                - generic [ref=e150]: Ana_M vs Guest-4821
                - generic [ref=e151]: Marchland · काल
              - generic [ref=e152]: खोली
              - generic [ref=e153]: १७ मिनि
              - generic [ref=e154]: ✓ सत्यापित
            - link "Bradley vs Mirelle Even Ground Classic · २ दिवसांपूर्वी 1 vs 1 ranked १८ मिनि ✓ सत्यापित" [ref=e156] [cursor=pointer]:
              - /url: /matches/c56ad527-5a24-45e4-b24b-0097e049a840
              - generic [ref=e160]:
                - generic [ref=e161]: Bradley vs Mirelle
                - generic [ref=e162]: Even Ground Classic · २ दिवसांपूर्वी
              - generic [ref=e163]: 1 vs 1 ranked
              - generic [ref=e164]: १८ मिनि
              - generic [ref=e165]: ✓ सत्यापित
            - link "Kestrel vs Nicowar (AI) Symmetric Arena · ३ दिवसांपूर्वी 1 vs 1 ranked ११ मिनि ✓ सत्यापित" [ref=e167] [cursor=pointer]:
              - /url: /matches/07e98edb-57b1-4494-bc56-6083b489ef8c
              - generic [ref=e171]:
                - generic [ref=e172]: Kestrel vs Nicowar (AI)
                - generic [ref=e173]: Symmetric Arena · ३ दिवसांपूर्वी
              - generic [ref=e174]: 1 vs 1 ranked
              - generic [ref=e175]: ११ मिनि
              - generic [ref=e176]: ✓ सत्यापित
            - link "Bradley vs Kestrel Even Ground Classic · ४ दिवसांपूर्वी 1 vs 1 ranked १७ मिनि ✓ सत्यापित" [ref=e178] [cursor=pointer]:
              - /url: /matches/1bada308-b372-4e7e-a9c9-818239ac7e40
              - generic [ref=e182]:
                - generic [ref=e183]: Bradley vs Kestrel
                - generic [ref=e184]: Even Ground Classic · ४ दिवसांपूर्वी
              - generic [ref=e185]: 1 vs 1 ranked
              - generic [ref=e186]: १७ मिनि
              - generic [ref=e187]: ✓ सत्यापित
            - link "Bradley vs Mirelle Even Ground Classic · ६ दिवसांपूर्वी 1 vs 1 ranked १६ मिनि ✓ सत्यापित" [ref=e189] [cursor=pointer]:
              - /url: /matches/0c1be6df-26e8-4317-8658-80ff9ff335d7
              - generic [ref=e193]:
                - generic [ref=e194]: Bradley vs Mirelle
                - generic [ref=e195]: Even Ground Classic · ६ दिवसांपूर्वी
              - generic [ref=e196]: 1 vs 1 ranked
              - generic [ref=e197]: १६ मिनि
              - generic [ref=e198]: ✓ सत्यापित
        - region [ref=e200]:
          - generic [ref=e201]:
            - heading "खेळाडूंना आवडणारे नकाशे" [level=2] [ref=e202]
            - link "नकाशे ब्राउझ करा" [ref=e203] [cursor=pointer]:
              - /url: /maps
          - generic [ref=e204]:
            - 'link "Isles of Plenty ४ संघ · लेखक: Ana_M · ♥ 14" [ref=e205] [cursor=pointer]':
              - /url: /maps/97d389fd-759b-4792-ac5d-a432307ad9a2
              - generic [ref=e206]: Isles of Plenty
              - generic [ref=e207]: "४ संघ · लेखक: Ana_M · ♥ 14"
            - 'link "Canal Country ४ संघ · लेखक: Mirelle · ♥ 9" [ref=e208] [cursor=pointer]':
              - /url: /maps/7a2fc50f-8b5e-4ea4-8914-3bfb05a22c4a
              - generic [ref=e209]: Canal Country
              - generic [ref=e210]: "४ संघ · लेखक: Mirelle · ♥ 9"
            - 'link "Honeycomb Isle ४ संघ · लेखक: Bradley · ♥ 6" [ref=e211] [cursor=pointer]':
              - /url: /maps/62f46559-3694-4c53-9a6a-37228ebc270c
              - generic [ref=e212]: Honeycomb Isle
              - generic [ref=e213]: "४ संघ · लेखक: Bradley · ♥ 6"
            - 'link "Crater Lakes ४ संघ · लेखक: Mirelle · ♥ 4" [ref=e214] [cursor=pointer]':
              - /url: /maps/6a893303-ab51-48fc-aab3-20b667d0ef6f
              - generic [ref=e215]: Crater Lakes
              - generic [ref=e216]: "४ संघ · लेखक: Mirelle · ♥ 4"
        - region [ref=e217]:
          - generic [ref=e218]:
            - heading "लीडरबोर्ड" [level=2] [ref=e219]
            - link "सर्व क्रमवारी" [ref=e220] [cursor=pointer]:
              - /url: /leaderboard
          - generic [ref=e221]:
            - generic [ref=e222]:
              - generic [ref=e223]:
                - heading "1 vs 1 ranked" [level=3] [ref=e224]
                - link "संपूर्ण लीडरबोर्ड" [ref=e225] [cursor=pointer]:
                  - /url: /leaderboard/ranked-1v1
              - list [ref=e226]:
                - listitem [ref=e227]:
                  - generic [aria-hidden] [ref=e228]: "1"
                  - generic [aria-hidden] [ref=e229]: K
                  - link "Kestrel" [ref=e231] [cursor=pointer]:
                    - /url: /players/5ea2f445-3411-41ce-b602-c98800302b50
                  - generic [ref=e232]: १७९८
                - listitem [ref=e233]:
                  - generic [aria-hidden] [ref=e234]: "2"
                  - generic [aria-hidden] [ref=e235]: M
                  - link "Mirelle" [ref=e237] [cursor=pointer]:
                    - /url: /players/53d46a4d-35b2-47f4-8ea6-1154543d4575
                  - generic [ref=e238]: १६३६
                - listitem [ref=e239]:
                  - generic [aria-hidden] [ref=e240]: "3"
                  - generic [aria-hidden] [ref=e241]: B
                  - link "Bradley" [ref=e243] [cursor=pointer]:
                    - /url: /players/29a1a50f-bbd1-4483-8aec-4b48b05eb086
                  - generic [ref=e244]: १५८६
                - listitem [ref=e245]:
                  - generic [aria-hidden] [ref=e246]: "4"
                  - generic [aria-hidden] [ref=e247]: A
                  - link "Ana_M" [ref=e249] [cursor=pointer]:
                    - /url: /players/dde415c2-36a6-413f-a892-1dc3ea885ff3
                  - generic [ref=e250]: १४१८
            - generic [ref=e251]:
              - generic [ref=e252]:
                - heading "2 vs 2 ranked" [level=3] [ref=e253]
                - link "संपूर्ण लीडरबोर्ड" [ref=e254] [cursor=pointer]:
                  - /url: /leaderboard/ranked-2v2
              - list [ref=e255]:
                - listitem [ref=e256]:
                  - generic [aria-hidden] [ref=e257]: "1"
                  - generic [aria-hidden] [ref=e258]: B
                  - link "Bradley" [ref=e260] [cursor=pointer]:
                    - /url: /players/29a1a50f-bbd1-4483-8aec-4b48b05eb086
                  - generic [ref=e261]: १४९१
        - generic [ref=e262]:
          - generic [ref=e263]:
            - heading "Globulation 2 प्रथमच खेळत आहात का?" [level=3] [ref=e264]
            - paragraph [ref=e265]: उद्दिष्टे ठरवा की तुमचे ग्लॉब्स कामाला लागतील. वसाहतीला अन्न द्या, युनिटना प्रशिक्षण द्या आणि एकत्र शोधमोहीम करा.
          - generic [ref=e266]:
            - heading "हा सर्व्हर" [level=3] [ref=e267]
            - generic [ref=e268]:
              - term [ref=e269]: पत्ता
              - definition [ref=e270]: http://127.0.0.1:4587
              - term [ref=e271]: गेम आवृत्त्या
              - definition [ref=e272]: स्वरूप 125, नेटवर्क 49
```

# Test source

```ts
  1   | import { mkdirSync, readFileSync } from 'node:fs';
  2   | import { resolve } from 'node:path';
  3   | import { expect, test } from '@playwright/test';
  4   | import { locales } from '../../../packages/i18n/src/locales.ts';
  5   | 
  6   | for (const locale of locales) {
  7   |   test(`home loads and renders ${locale.code}`, async ({ page, context }, info) => {
  8   |     // A guest keeps the heading independent of the seeded API's current account.
  9   |     await page.route('**/api/v1/accounts/me', (route) =>
  10  |       route.fulfill({
  11  |         status: 401,
  12  |         json: { code: 'unauthenticated', message: 'Sign in required.' },
  13  |       }),
  14  |     );
  15  |     const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  16  |     await context.addCookies([{ name: 'glob2_locale', value: locale.code, url: base }]);
  17  |     await page.goto('/');
  18  |     await expect(page.locator('html')).toHaveAttribute('lang', locale.code);
  19  |     await expect(page.locator('html')).toHaveAttribute('dir', locale.dir);
  20  |     await expect(page.getByTestId('language-selector').first()).toHaveValue(locale.code);
  21  |     if (locale.code === 'mr') {
  22  |       const geometry = await page
  23  |         .getByTestId('language-selector')
  24  |         .first()
  25  |         .evaluate((element) => {
  26  |           const select = element as HTMLSelectElement;
  27  |           const style = getComputedStyle(select);
  28  |           const compact = select.parentElement!.querySelector('.language-preference-compact')!;
  29  |           const compactVisible = getComputedStyle(compact).display !== 'none';
  30  |           const label = compactVisible
  31  |             ? compact.textContent!.trim()
  32  |             : select.selectedOptions[0]!.text;
  33  |           const canvas = document.createElement('canvas');
  34  |           const context = canvas.getContext('2d')!;
  35  |           context.font = compactVisible ? getComputedStyle(compact).font : style.font;
  36  |           const available =
  37  |             select.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
  38  |           return {
  39  |             label,
  40  |             fullName: select.selectedOptions[0]!.text,
  41  |             textWidth: context.measureText(label).width,
  42  |             available,
  43  |           };
  44  |         });
  45  |       expect(geometry.fullName).toBe('मराठी');
  46  |       expect(['मराठी', 'MR']).toContain(geometry.label);
  47  |       expect(geometry.textWidth).toBeLessThanOrEqual(geometry.available);
  48  |     }
  49  |     const catalog = JSON.parse(
  50  |       readFileSync(
  51  |         new URL(`../../../packages/i18n/locales/${locale.code}.json`, import.meta.url),
  52  |         'utf8',
  53  |       ),
  54  |     ) as Record<string, string>;
  55  |     expect(catalog['Welcome to the colony']).toBeTruthy();
  56  |     const heading = catalog['Welcome to the colony'] ?? 'Welcome to the colony';
  57  |     if (locale.code !== 'en') expect(heading).not.toBe('Welcome to the colony');
  58  |     await expect(page.locator('#hero-title')).toHaveText(heading);
  59  |     await expect(page.getByRole('link', { name: /Isles of Plenty/ })).toBeVisible();
  60  |     await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
  61  |     // Skins is an instance feature; required navigation stays available in every locale.
  62  |     expect(
  63  |       await page
  64  |         .locator('.app-sidebar .nav a')
  65  |         .evaluateAll((links) => links.map((link) => link.getAttribute('href'))),
  66  |     ).toEqual(
  67  |       expect.arrayContaining([
  68  |         '/',
  69  |         '/leaderboard',
  70  |         '/players',
  71  |         '/matches',
  72  |         '/maps',
  73  |         '/sets',
  74  |         '/ais',
  75  |         '/buildings',
  76  |         '/music',
  77  |       ]),
  78  |     );
  79  |     const signIn = page.locator('.app-sidebar .sidebar-account a[href="/signin"]');
  80  |     await expect(signIn).toBeVisible();
  81  |     expect(
  82  |       await signIn.evaluate((element) => element.scrollWidth - element.clientWidth),
  83  |     ).toBeLessThanOrEqual(1);
  84  |     // Long translations and RTL layout must fit the viewport on phones as well as desktops.
  85  |     const overflow = await page.evaluate(
  86  |       () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
  87  |     );
  88  |     expect(overflow).toBeLessThanOrEqual(1);
  89  |     let screenshotPath: string | undefined;
  90  |     if (process.env.SCREENSHOT_DIR) {
  91  |       const directory = resolve(process.env.SCREENSHOT_DIR, info.project.name);
  92  |       mkdirSync(directory, { recursive: true });
  93  |       screenshotPath = resolve(directory, `${locale.code}-home.png`);
  94  |     }
  95  |     await info.attach(`${locale.code}-home`, {
> 96  |       body: await page.screenshot({ fullPage: true, path: screenshotPath }),
      |                        ^ Error: page.screenshot: Protocol error (Page.captureScreenshot): Unable to capture screenshot
  97  |       contentType: 'image/png',
  98  |     });
  99  |     await page.reload();
  100 |     await expect(page.locator('html')).toHaveAttribute('lang', locale.code);
  101 |   });
  102 | }
  103 | 
  104 | test('changing the selected language updates the page without losing input', async ({ page }) => {
  105 |   await page.route('**/api/v1/accounts/me', (route) =>
  106 |     route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  107 |   );
  108 |   await page.goto('/');
  109 |   const code = page.locator('input[name="code"]');
  110 |   await code.fill('ABCD');
  111 |   if (!(await page.getByTestId('language-selector').first().isVisible()))
  112 |     await page.locator('.rail-expand').click();
  113 |   const selector = page.locator('[data-testid="language-selector"]:visible').first();
  114 |   await selector.selectOption('fr');
  115 |   await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  116 |   await expect(code).toHaveValue('ABCD');
  117 |   await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
  118 | });
  119 | 
  120 | // Exercise script-font loading and an explicit saved choice, beyond inventory smoke tests.
  121 | test('Marathi switching restores LTR, loads the bundled font and keeps the chosen locale', async ({
  122 |   page,
  123 |   context,
  124 | }, info) => {
  125 |   await page.route('**/api/v1/accounts/me', (route) =>
  126 |     route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  127 |   );
  128 |   const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  129 |   await context.addCookies([{ name: 'glob2_locale', value: 'ar', url: base }]);
  130 |   await page.goto('/');
  131 |   await expect(page.locator('html')).toHaveAttribute('dir', 'rtl');
  132 |   const code = page.locator('input[name="code"]');
  133 |   await code.fill('MARATHI');
  134 |   if (!(await page.getByTestId('language-selector').first().isVisible()))
  135 |     await page.locator('.rail-expand').click();
  136 |   await page.locator('[data-testid="language-selector"]:visible').first().selectOption('mr');
  137 |   await expect(page.locator('html')).toHaveAttribute('lang', 'mr');
  138 |   await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
  139 |   await expect(page.getByTestId('theme-toggle')).toHaveAttribute('aria-label', /थीम बदला$/);
  140 |   await expect(
  141 |     page.locator('[data-testid="language-selector"]:visible').last().locator('option:checked'),
  142 |   ).toHaveText('मराठी');
  143 |   await expect(code).toHaveValue('MARATHI');
  144 |   expect(
  145 |     await page.locator('#hero-title').evaluate((element) => getComputedStyle(element).fontFamily),
  146 |   ).toContain('Glob2 Scripts');
  147 |   const loaded = await page.evaluate(async () => {
  148 |     const faces = await document.fonts.load('20px "Glob2 Scripts"', 'मराठी ळ ऱ ऍ ऑ क्ष ज्ञ श्र');
  149 |     return faces.some(
  150 |       (face) => face.family.replaceAll(/["']/g, '') === 'Glob2 Scripts' && face.status === 'loaded',
  151 |     );
  152 |   });
  153 |   expect(loaded).toBe(true);
  154 |   await page.reload();
  155 |   await expect(page.locator('html')).toHaveAttribute('lang', 'mr');
  156 |   await expect(page.getByTestId('language-selector').first()).toHaveValue('mr');
  157 | });
  158 | 
```