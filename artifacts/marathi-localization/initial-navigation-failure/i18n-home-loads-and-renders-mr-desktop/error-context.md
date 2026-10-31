# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: i18n.spec.ts >> home loads and renders mr
- Location: e2e/i18n.spec.ts:7:3

# Error details

```
Error: expect(locator).toHaveCount(expected) failed

Locator:  locator('.app-sidebar .nav a')
Expected: 10
Received: 9
Timeout:  5000ms

Call log:
  - Expect "toHaveCount" locator('.app-sidebar .nav a') with timeout 5000ms
  - waiting for locator('.app-sidebar .nav a')
    13 × locator resolved to 9 elements
       - unexpected value "9"

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
        - 'button "थीम: या डिव्हाइसप्रमाणेच. Change theme" [ref=e83] [cursor=pointer]'
        - generic [ref=e88]:
          - text: भाषा
          - combobox "भाषा" [ref=e89]:
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
      - link "साइन इन करा" [ref=e91] [cursor=pointer]:
        - /url: /signin
  - main [ref=e92]:
    - generic [ref=e94]:
      - region [ref=e95]:
        - button "❚❚ ग्लॉब्सना थांबवा" [ref=e98] [cursor=pointer]
        - generic [ref=e100]:
          - heading "वसाहतीत आपले स्वागत आहे" [level=1] [ref=e101]
          - paragraph [ref=e102]: तुमची पुढची वसाहत येथून सुरू होते. तुमच्या ब्राउझरमध्ये खेळा किंवा तुमच्या मित्रांसोबत सामील व्हा.
          - generic [ref=e103]:
            - link "ब्राउझरमध्ये खेळा" [ref=e104] [cursor=pointer]:
              - /url: /play/
            - link "डाउनलोड करा" [ref=e107] [cursor=pointer]:
              - /url: https://github.com/Globulation2/glob2/releases
          - generic [ref=e110]:
            - generic [ref=e111]:
              - text: तुम्हाला इन्व्हाइट कोड मिळाला आहे का?
              - textbox "तुम्हाला इन्व्हाइट कोड मिळाला आहे का?" [ref=e112]:
                - /placeholder: कोड किंवा आमंत्रण लिंक
            - button "सामील व्हा" [ref=e113] [cursor=pointer]
            - paragraph [ref=e114]: मित्र त्यांच्या खोलीतून एक कोड किंवा लिंक शेअर करतात.
          - list [ref=e115]:
            - listitem [ref=e116]:
              - generic [ref=e118]: १
              - generic [ref=e119]: खेळाडू ऑनलाइन
            - listitem [ref=e120]:
              - generic [ref=e121]: १
              - generic [ref=e122]: सामना आता सुरू आहे
            - listitem [ref=e123]:
              - generic [ref=e124]: ३
              - generic [ref=e125]: आजचे सामने
          - paragraph [ref=e126]: रूम्स आणि कॅज्युअल गेम्ससाठी खात्याची आवश्यकता नाही; रँक मिळवण्यासाठी साइन इन करा.
      - generic [ref=e127]:
        - region [ref=e128]:
          - generic [ref=e129]:
            - heading "अलीकडील सामने" [level=2] [ref=e130]
            - link "सर्व सामने" [ref=e131] [cursor=pointer]:
              - /url: /matches
          - generic [ref=e132]:
            - link "Bradley vs Kestrel Even Ground · आज ०४:५९ PM 1 vs 1 ranked ११ मिनि तपासत आहे…" [ref=e133] [cursor=pointer]:
              - /url: /matches/a7e639b8-91c8-4157-b849-acbf48b1b348
              - generic [ref=e137]:
                - generic [ref=e138]: Bradley vs Kestrel
                - generic [ref=e139]: Even Ground · आज ०४:५९ PM
              - generic [ref=e140]: 1 vs 1 ranked
              - generic [ref=e141]: ११ मिनि
              - generic [ref=e142]: तपासत आहे…
            - link "Ana_M vs Guest-4821 Marchland · काल खोली १७ मिनि ✓ सत्यापित" [ref=e144] [cursor=pointer]:
              - /url: /matches/120233e9-8518-41b2-ac0b-585a11f410c5
              - generic [ref=e148]:
                - generic [ref=e149]: Ana_M vs Guest-4821
                - generic [ref=e150]: Marchland · काल
              - generic [ref=e151]: खोली
              - generic [ref=e152]: १७ मिनि
              - generic [ref=e153]: ✓ सत्यापित
            - link "Bradley vs Mirelle Even Ground Classic · २ दिवसांपूर्वी 1 vs 1 ranked १८ मिनि ✓ सत्यापित" [ref=e155] [cursor=pointer]:
              - /url: /matches/0f49fd5a-bf7b-449c-92fe-d967e9851d99
              - generic [ref=e159]:
                - generic [ref=e160]: Bradley vs Mirelle
                - generic [ref=e161]: Even Ground Classic · २ दिवसांपूर्वी
              - generic [ref=e162]: 1 vs 1 ranked
              - generic [ref=e163]: १८ मिनि
              - generic [ref=e164]: ✓ सत्यापित
            - link "Kestrel vs Nicowar (AI) Symmetric Arena · ३ दिवसांपूर्वी 1 vs 1 ranked ११ मिनि ✓ सत्यापित" [ref=e166] [cursor=pointer]:
              - /url: /matches/9309be32-c74c-4639-8584-b327d9c639a7
              - generic [ref=e170]:
                - generic [ref=e171]: Kestrel vs Nicowar (AI)
                - generic [ref=e172]: Symmetric Arena · ३ दिवसांपूर्वी
              - generic [ref=e173]: 1 vs 1 ranked
              - generic [ref=e174]: ११ मिनि
              - generic [ref=e175]: ✓ सत्यापित
            - link "Bradley vs Kestrel Even Ground Classic · ४ दिवसांपूर्वी 1 vs 1 ranked १७ मिनि ✓ सत्यापित" [ref=e177] [cursor=pointer]:
              - /url: /matches/f38872ae-4b10-4ff3-8be7-b40bc3cfe239
              - generic [ref=e181]:
                - generic [ref=e182]: Bradley vs Kestrel
                - generic [ref=e183]: Even Ground Classic · ४ दिवसांपूर्वी
              - generic [ref=e184]: 1 vs 1 ranked
              - generic [ref=e185]: १७ मिनि
              - generic [ref=e186]: ✓ सत्यापित
            - link "Bradley vs Mirelle Even Ground Classic · ६ दिवसांपूर्वी 1 vs 1 ranked १६ मिनि ✓ सत्यापित" [ref=e188] [cursor=pointer]:
              - /url: /matches/ca7fdb18-fe91-4817-94cd-2a8180766d9f
              - generic [ref=e192]:
                - generic [ref=e193]: Bradley vs Mirelle
                - generic [ref=e194]: Even Ground Classic · ६ दिवसांपूर्वी
              - generic [ref=e195]: 1 vs 1 ranked
              - generic [ref=e196]: १६ मिनि
              - generic [ref=e197]: ✓ सत्यापित
        - region [ref=e199]:
          - generic [ref=e200]:
            - heading "खेळाडूंना आवडणारे नकाशे" [level=2] [ref=e201]
            - link "नकाशे ब्राउझ करा" [ref=e202] [cursor=pointer]:
              - /url: /maps
          - generic [ref=e203]:
            - 'link "Isles of Plenty ४ संघ · लेखक: Ana_M · ♥ 14" [ref=e204] [cursor=pointer]':
              - /url: /maps/c80190de-0acb-4677-826c-7aa01400b2f3
              - generic [ref=e205]: Isles of Plenty
              - generic [ref=e206]: "४ संघ · लेखक: Ana_M · ♥ 14"
            - 'link "Canal Country ४ संघ · लेखक: Mirelle · ♥ 9" [ref=e207] [cursor=pointer]':
              - /url: /maps/4bf1c1f1-9e51-494c-aecb-746808a03b8e
              - generic [ref=e208]: Canal Country
              - generic [ref=e209]: "४ संघ · लेखक: Mirelle · ♥ 9"
            - 'link "Honeycomb Isle ४ संघ · लेखक: Bradley · ♥ 6" [ref=e210] [cursor=pointer]':
              - /url: /maps/5c71bbb4-9da2-4194-b731-afa92a0416b3
              - generic [ref=e211]: Honeycomb Isle
              - generic [ref=e212]: "४ संघ · लेखक: Bradley · ♥ 6"
            - 'link "Crater Lakes ४ संघ · लेखक: Mirelle · ♥ 4" [ref=e213] [cursor=pointer]':
              - /url: /maps/3c50f3f3-13fd-4315-89dd-90d5f0d48050
              - generic [ref=e214]: Crater Lakes
              - generic [ref=e215]: "४ संघ · लेखक: Mirelle · ♥ 4"
        - region [ref=e216]:
          - generic [ref=e217]:
            - heading "लीडरबोर्ड" [level=2] [ref=e218]
            - link "सर्व क्रमवारी" [ref=e219] [cursor=pointer]:
              - /url: /leaderboard
          - generic [ref=e220]:
            - generic [ref=e221]:
              - generic [ref=e222]:
                - heading "1 vs 1 ranked" [level=3] [ref=e223]
                - link "संपूर्ण लीडरबोर्ड" [ref=e224] [cursor=pointer]:
                  - /url: /leaderboard/ranked-1v1
              - list [ref=e225]:
                - listitem [ref=e226]:
                  - generic [aria-hidden] [ref=e227]: "1"
                  - generic [aria-hidden] [ref=e228]: K
                  - link "Kestrel" [ref=e230] [cursor=pointer]:
                    - /url: /players/8c288c14-5d40-4e1d-af05-653f9c327fff
                  - generic [ref=e231]: १७९८
                - listitem [ref=e232]:
                  - generic [aria-hidden] [ref=e233]: "2"
                  - generic [aria-hidden] [ref=e234]: M
                  - link "Mirelle" [ref=e236] [cursor=pointer]:
                    - /url: /players/7490d2db-0a9b-4252-bb65-c5b50903d61c
                  - generic [ref=e237]: १६३६
                - listitem [ref=e238]:
                  - generic [aria-hidden] [ref=e239]: "3"
                  - generic [aria-hidden] [ref=e240]: B
                  - link "Bradley" [ref=e242] [cursor=pointer]:
                    - /url: /players/0f93b4ea-2782-4a91-8fc9-7c1dae11fe54
                  - generic [ref=e243]: १५८६
                - listitem [ref=e244]:
                  - generic [aria-hidden] [ref=e245]: "4"
                  - generic [aria-hidden] [ref=e246]: A
                  - link "Ana_M" [ref=e248] [cursor=pointer]:
                    - /url: /players/a92d65d1-c87c-4c3f-a9af-0bb243ca40b5
                  - generic [ref=e249]: १४१८
            - generic [ref=e250]:
              - generic [ref=e251]:
                - heading "2 vs 2 ranked" [level=3] [ref=e252]
                - link "संपूर्ण लीडरबोर्ड" [ref=e253] [cursor=pointer]:
                  - /url: /leaderboard/ranked-2v2
              - list [ref=e254]:
                - listitem [ref=e255]:
                  - generic [aria-hidden] [ref=e256]: "1"
                  - generic [aria-hidden] [ref=e257]: B
                  - link "Bradley" [ref=e259] [cursor=pointer]:
                    - /url: /players/0f93b4ea-2782-4a91-8fc9-7c1dae11fe54
                  - generic [ref=e260]: १४९१
        - generic [ref=e261]:
          - generic [ref=e262]:
            - heading "Globulation 2 प्रथमच खेळत आहात का?" [level=3] [ref=e263]
            - paragraph [ref=e264]: उद्दिष्टे ठरवा की तुमचे ग्लॉब्स कामाला लागतील. वसाहतीला अन्न द्या, युनिटना प्रशिक्षण द्या आणि एकत्र शोधमोहीम करा.
          - generic [ref=e265]:
            - heading "हा सर्व्हर" [level=3] [ref=e266]
            - generic [ref=e267]:
              - term [ref=e268]: पत्ता
              - definition [ref=e269]: http://127.0.0.1:4587
              - term [ref=e270]: गेम आवृत्त्या
              - definition [ref=e271]: स्वरूप 125, नेटवर्क 49
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
  21  |     const catalog = JSON.parse(
  22  |       readFileSync(
  23  |         new URL(`../../../packages/i18n/locales/${locale.code}.json`, import.meta.url),
  24  |         'utf8',
  25  |       ),
  26  |     ) as Record<string, string>;
  27  |     expect(catalog['Welcome to the colony']).toBeTruthy();
  28  |     const heading = catalog['Welcome to the colony'] ?? 'Welcome to the colony';
  29  |     if (locale.code !== 'en') expect(heading).not.toBe('Welcome to the colony');
  30  |     await expect(page.locator('#hero-title')).toHaveText(heading);
  31  |     await expect(page.getByRole('link', { name: /Isles of Plenty/ })).toBeVisible();
  32  |     await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
> 33  |     await expect(page.locator('.app-sidebar .nav a')).toHaveCount(10);
      |                                                       ^ Error: expect(locator).toHaveCount(expected) failed
  34  |     const signIn = page.locator('.app-sidebar .sidebar-account a[href="/signin"]');
  35  |     await expect(signIn).toBeVisible();
  36  |     expect(
  37  |       await signIn.evaluate((element) => element.scrollWidth - element.clientWidth),
  38  |     ).toBeLessThanOrEqual(1);
  39  |     // Long translations and RTL layout must fit the viewport on phones as well as desktops.
  40  |     const overflow = await page.evaluate(
  41  |       () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
  42  |     );
  43  |     expect(overflow).toBeLessThanOrEqual(1);
  44  |     let screenshotPath: string | undefined;
  45  |     if (process.env.SCREENSHOT_DIR) {
  46  |       const directory = resolve(process.env.SCREENSHOT_DIR, info.project.name);
  47  |       mkdirSync(directory, { recursive: true });
  48  |       screenshotPath = resolve(directory, `${locale.code}-home.png`);
  49  |     }
  50  |     await info.attach(`${locale.code}-home`, {
  51  |       body: await page.screenshot({ fullPage: true, path: screenshotPath }),
  52  |       contentType: 'image/png',
  53  |     });
  54  |     await page.reload();
  55  |     await expect(page.locator('html')).toHaveAttribute('lang', locale.code);
  56  |   });
  57  | }
  58  | 
  59  | test('changing the selected language updates the page without losing input', async ({ page }) => {
  60  |   await page.route('**/api/v1/accounts/me', (route) =>
  61  |     route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  62  |   );
  63  |   await page.goto('/');
  64  |   const code = page.locator('input[name="code"]');
  65  |   await code.fill('ABCD');
  66  |   if (!(await page.getByTestId('language-selector').first().isVisible()))
  67  |     await page.locator('.rail-expand').click();
  68  |   const selector = page.locator('[data-testid="language-selector"]:visible').first();
  69  |   await selector.selectOption('fr');
  70  |   await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  71  |   await expect(code).toHaveValue('ABCD');
  72  |   await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
  73  | });
  74  | 
  75  | // Exercise script-font loading and an explicit saved choice, beyond inventory smoke tests.
  76  | test('Marathi switching restores LTR, loads the bundled font and keeps the chosen locale', async ({
  77  |   page,
  78  |   context,
  79  | }, info) => {
  80  |   await page.route('**/api/v1/accounts/me', (route) =>
  81  |     route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  82  |   );
  83  |   const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  84  |   await context.addCookies([{ name: 'glob2_locale', value: 'ar', url: base }]);
  85  |   await page.goto('/');
  86  |   await expect(page.locator('html')).toHaveAttribute('dir', 'rtl');
  87  |   const code = page.locator('input[name="code"]');
  88  |   await code.fill('MARATHI');
  89  |   if (!(await page.getByTestId('language-selector').first().isVisible()))
  90  |     await page.locator('.rail-expand').click();
  91  |   await page.locator('[data-testid="language-selector"]:visible').first().selectOption('mr');
  92  |   await expect(page.locator('html')).toHaveAttribute('lang', 'mr');
  93  |   await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
  94  |   await expect(code).toHaveValue('MARATHI');
  95  |   expect(
  96  |     await page.locator('#hero-title').evaluate((element) => getComputedStyle(element).fontFamily),
  97  |   ).toContain('Glob2 Scripts');
  98  |   const loaded = await page.evaluate(async () => {
  99  |     const faces = await document.fonts.load('20px "Glob2 Scripts"', 'मराठी ळ ऱ ऍ ऑ क्ष ज्ञ श्र');
  100 |     return faces.some(
  101 |       (face) => face.family.replaceAll(/["']/g, '') === 'Glob2 Scripts' && face.status === 'loaded',
  102 |     );
  103 |   });
  104 |   expect(loaded).toBe(true);
  105 |   await page.reload();
  106 |   await expect(page.locator('html')).toHaveAttribute('lang', 'mr');
  107 |   await expect(page.getByTestId('language-selector').first()).toHaveValue('mr');
  108 | });
  109 | 
```