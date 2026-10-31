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
    - link "Glob2 Online (test), मुख्यपृष्ठ" [ref=e7] [cursor=pointer]:
      - /url: /
    - link "ब्राउझरमध्ये खेळा" [ref=e8] [cursor=pointer]:
      - /url: /play/
    - navigation "मुख्य" [ref=e11]:
      - generic [ref=e12]:
        - link "मुख्यपृष्ठ" [ref=e13] [cursor=pointer]:
          - /url: /
        - link "लीडरबोर्ड" [ref=e14] [cursor=pointer]:
          - /url: /leaderboard
        - link "खेळाडू" [ref=e20] [cursor=pointer]:
          - /url: /players
        - link "सामने" [ref=e27] [cursor=pointer]:
          - /url: /matches
      - generic [ref=e34]:
        - link "नकाशे" [ref=e35] [cursor=pointer]:
          - /url: /maps
        - link "भूभाग आणि संसाधने" [ref=e39] [cursor=pointer]:
          - /url: /sets
        - link "एआय लायब्ररी" [ref=e46] [cursor=pointer]:
          - /url: /ais
        - link "इमारती" [ref=e52] [cursor=pointer]:
          - /url: /buildings
        - link "संगीत" [ref=e56] [cursor=pointer]:
          - /url: /music
    - generic [ref=e62]:
      - generic [ref=e63]:
        - group [ref=e64]:
          - generic "माहिती आणि मदत" [ref=e65]
        - 'button "थीम: या डिव्हाइसप्रमाणेच. Change theme" [ref=e66] [cursor=pointer]'
        - generic [ref=e71]:
          - text: भाषा
          - combobox "भाषा" [ref=e72]:
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
      - link "साइन इन करा" [ref=e74] [cursor=pointer]:
        - /url: /signin
  - button "नेव्हिगेशन उघडा" [ref=e75] [cursor=pointer]
  - main [ref=e79]:
    - generic [ref=e81]:
      - region [ref=e82]:
        - button "❚❚ ग्लॉब्सना थांबवा" [ref=e85] [cursor=pointer]
        - generic [ref=e87]:
          - heading "वसाहतीत आपले स्वागत आहे" [level=1] [ref=e88]
          - paragraph [ref=e89]: तुमची पुढची वसाहत येथून सुरू होते. तुमच्या ब्राउझरमध्ये खेळा किंवा तुमच्या मित्रांसोबत सामील व्हा.
          - generic [ref=e90]:
            - link "ब्राउझरमध्ये खेळा" [ref=e91] [cursor=pointer]:
              - /url: /play/
            - link "डाउनलोड करा" [ref=e94] [cursor=pointer]:
              - /url: https://github.com/Globulation2/glob2/releases
          - generic [ref=e97]:
            - generic [ref=e98]:
              - text: तुम्हाला इन्व्हाइट कोड मिळाला आहे का?
              - textbox "तुम्हाला इन्व्हाइट कोड मिळाला आहे का?" [ref=e99]:
                - /placeholder: कोड किंवा आमंत्रण लिंक
            - button "सामील व्हा" [ref=e100] [cursor=pointer]
            - paragraph [ref=e101]: मित्र त्यांच्या खोलीतून एक कोड किंवा लिंक शेअर करतात.
          - list [ref=e102]:
            - listitem [ref=e103]:
              - generic [ref=e105]: १
              - generic [ref=e106]: खेळाडू ऑनलाइन
            - listitem [ref=e107]:
              - generic [ref=e108]: १
              - generic [ref=e109]: सामना आता सुरू आहे
            - listitem [ref=e110]:
              - generic [ref=e111]: ३
              - generic [ref=e112]: आजचे सामने
          - paragraph [ref=e113]: रूम्स आणि कॅज्युअल गेम्ससाठी खात्याची आवश्यकता नाही; रँक मिळवण्यासाठी साइन इन करा.
      - generic [ref=e114]:
        - region [ref=e115]:
          - generic [ref=e116]:
            - heading "अलीकडील सामने" [level=2] [ref=e117]
            - link "सर्व सामने" [ref=e118] [cursor=pointer]:
              - /url: /matches
          - generic [ref=e119]:
            - link "Bradley vs Kestrel Even Ground · आज ०४:५९ PM तपासत आहे…" [ref=e120] [cursor=pointer]:
              - /url: /matches/a7e639b8-91c8-4157-b849-acbf48b1b348
              - generic [ref=e124]:
                - generic [ref=e125]: Bradley vs Kestrel
                - generic [ref=e126]: Even Ground · आज ०४:५९ PM
              - generic [ref=e127]: तपासत आहे…
            - link "Ana_M vs Guest-4821 Marchland · काल ✓ सत्यापित" [ref=e129] [cursor=pointer]:
              - /url: /matches/120233e9-8518-41b2-ac0b-585a11f410c5
              - generic [ref=e133]:
                - generic [ref=e134]: Ana_M vs Guest-4821
                - generic [ref=e135]: Marchland · काल
              - generic [ref=e136]: ✓ सत्यापित
            - link "Bradley vs Mirelle Even Ground Classic · २ दिवसांपूर्वी ✓ सत्यापित" [ref=e138] [cursor=pointer]:
              - /url: /matches/0f49fd5a-bf7b-449c-92fe-d967e9851d99
              - generic [ref=e142]:
                - generic [ref=e143]: Bradley vs Mirelle
                - generic [ref=e144]: Even Ground Classic · २ दिवसांपूर्वी
              - generic [ref=e145]: ✓ सत्यापित
            - link "Kestrel vs Nicowar (AI) Symmetric Arena · ३ दिवसांपूर्वी ✓ सत्यापित" [ref=e147] [cursor=pointer]:
              - /url: /matches/9309be32-c74c-4639-8584-b327d9c639a7
              - generic [ref=e151]:
                - generic [ref=e152]: Kestrel vs Nicowar (AI)
                - generic [ref=e153]: Symmetric Arena · ३ दिवसांपूर्वी
              - generic [ref=e154]: ✓ सत्यापित
            - link "Bradley vs Kestrel Even Ground Classic · ४ दिवसांपूर्वी ✓ सत्यापित" [ref=e156] [cursor=pointer]:
              - /url: /matches/f38872ae-4b10-4ff3-8be7-b40bc3cfe239
              - generic [ref=e160]:
                - generic [ref=e161]: Bradley vs Kestrel
                - generic [ref=e162]: Even Ground Classic · ४ दिवसांपूर्वी
              - generic [ref=e163]: ✓ सत्यापित
            - link "Bradley vs Mirelle Even Ground Classic · ६ दिवसांपूर्वी ✓ सत्यापित" [ref=e165] [cursor=pointer]:
              - /url: /matches/ca7fdb18-fe91-4817-94cd-2a8180766d9f
              - generic [ref=e169]:
                - generic [ref=e170]: Bradley vs Mirelle
                - generic [ref=e171]: Even Ground Classic · ६ दिवसांपूर्वी
              - generic [ref=e172]: ✓ सत्यापित
        - region [ref=e174]:
          - generic [ref=e175]:
            - heading "खेळाडूंना आवडणारे नकाशे" [level=2] [ref=e176]
            - link "नकाशे ब्राउझ करा" [ref=e177] [cursor=pointer]:
              - /url: /maps
          - generic [ref=e178]:
            - 'link "Isles of Plenty ४ संघ · लेखक: Ana_M · ♥ 14" [ref=e179] [cursor=pointer]':
              - /url: /maps/c80190de-0acb-4677-826c-7aa01400b2f3
              - generic [ref=e180]: Isles of Plenty
              - generic [ref=e181]: "४ संघ · लेखक: Ana_M · ♥ 14"
            - 'link "Canal Country ४ संघ · लेखक: Mirelle · ♥ 9" [ref=e182] [cursor=pointer]':
              - /url: /maps/4bf1c1f1-9e51-494c-aecb-746808a03b8e
              - generic [ref=e183]: Canal Country
              - generic [ref=e184]: "४ संघ · लेखक: Mirelle · ♥ 9"
            - 'link "Honeycomb Isle ४ संघ · लेखक: Bradley · ♥ 6" [ref=e185] [cursor=pointer]':
              - /url: /maps/5c71bbb4-9da2-4194-b731-afa92a0416b3
              - generic [ref=e186]: Honeycomb Isle
              - generic [ref=e187]: "४ संघ · लेखक: Bradley · ♥ 6"
            - 'link "Crater Lakes ४ संघ · लेखक: Mirelle · ♥ 4" [ref=e188] [cursor=pointer]':
              - /url: /maps/3c50f3f3-13fd-4315-89dd-90d5f0d48050
              - generic [ref=e189]: Crater Lakes
              - generic [ref=e190]: "४ संघ · लेखक: Mirelle · ♥ 4"
        - region [ref=e191]:
          - generic [ref=e192]:
            - heading "लीडरबोर्ड" [level=2] [ref=e193]
            - link "सर्व क्रमवारी" [ref=e194] [cursor=pointer]:
              - /url: /leaderboard
          - generic [ref=e195]:
            - generic [ref=e196]:
              - generic [ref=e197]:
                - heading "1 vs 1 ranked" [level=3] [ref=e198]
                - link "संपूर्ण लीडरबोर्ड" [ref=e199] [cursor=pointer]:
                  - /url: /leaderboard/ranked-1v1
              - list [ref=e200]:
                - listitem [ref=e201]:
                  - generic [aria-hidden] [ref=e202]: "1"
                  - generic [aria-hidden] [ref=e203]: K
                  - link "Kestrel" [ref=e205] [cursor=pointer]:
                    - /url: /players/8c288c14-5d40-4e1d-af05-653f9c327fff
                  - generic [ref=e206]: १७९८
                - listitem [ref=e207]:
                  - generic [aria-hidden] [ref=e208]: "2"
                  - generic [aria-hidden] [ref=e209]: M
                  - link "Mirelle" [ref=e211] [cursor=pointer]:
                    - /url: /players/7490d2db-0a9b-4252-bb65-c5b50903d61c
                  - generic [ref=e212]: १६३६
                - listitem [ref=e213]:
                  - generic [aria-hidden] [ref=e214]: "3"
                  - generic [aria-hidden] [ref=e215]: B
                  - link "Bradley" [ref=e217] [cursor=pointer]:
                    - /url: /players/0f93b4ea-2782-4a91-8fc9-7c1dae11fe54
                  - generic [ref=e218]: १५८६
                - listitem [ref=e219]:
                  - generic [aria-hidden] [ref=e220]: "4"
                  - generic [aria-hidden] [ref=e221]: A
                  - link "Ana_M" [ref=e223] [cursor=pointer]:
                    - /url: /players/a92d65d1-c87c-4c3f-a9af-0bb243ca40b5
                  - generic [ref=e224]: १४१८
            - generic [ref=e225]:
              - generic [ref=e226]:
                - heading "2 vs 2 ranked" [level=3] [ref=e227]
                - link "संपूर्ण लीडरबोर्ड" [ref=e228] [cursor=pointer]:
                  - /url: /leaderboard/ranked-2v2
              - list [ref=e229]:
                - listitem [ref=e230]:
                  - generic [aria-hidden] [ref=e231]: "1"
                  - generic [aria-hidden] [ref=e232]: B
                  - link "Bradley" [ref=e234] [cursor=pointer]:
                    - /url: /players/0f93b4ea-2782-4a91-8fc9-7c1dae11fe54
                  - generic [ref=e235]: १४९१
        - generic [ref=e236]:
          - generic [ref=e237]:
            - heading "Globulation 2 प्रथमच खेळत आहात का?" [level=3] [ref=e238]
            - paragraph [ref=e239]: उद्दिष्टे ठरवा की तुमचे ग्लॉब्स कामाला लागतील. वसाहतीला अन्न द्या, युनिटना प्रशिक्षण द्या आणि एकत्र शोधमोहीम करा.
          - generic [ref=e240]:
            - heading "हा सर्व्हर" [level=3] [ref=e241]
            - generic [ref=e242]:
              - term [ref=e243]: पत्ता
              - definition [ref=e244]: http://127.0.0.1:4587
              - term [ref=e245]: गेम आवृत्त्या
              - definition [ref=e246]: स्वरूप 125, नेटवर्क 49
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