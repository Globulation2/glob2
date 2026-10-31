import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test, type Page, type TestInfo } from '@playwright/test';
import { INVITE_CODE } from './showcase.ts';

const catalog = JSON.parse(
  readFileSync(new URL('../../../packages/i18n/locales/ta.json', import.meta.url), 'utf8'),
) as Record<string, string>;

async function capture(page: Page, info: TestInfo, path: string, name: string) {
  await page.goto(path);
  await expect(page.locator('html')).toHaveAttribute('lang', 'ta');
  await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
  await page.evaluate(() => document.fonts.ready);
  expect(
    await page.evaluate(() => document.fonts.check('16px "Glob2 Scripts"', 'தமிழ் கொடி கூடு')),
  ).toBe(true);
  expect(await page.evaluate(() => getComputedStyle(document.body).fontFamily)).toContain(
    'Glob2 Scripts',
  );
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    ),
    `horizontal overflow at ${path}`,
  ).toBeLessThanOrEqual(1);
  expect(
    await page.locator('.sidebar-play, .stat .k').evaluateAll((elements) =>
      elements
        .filter((element) => element.getBoundingClientRect().width > 0)
        .filter((element) => element.scrollWidth - element.clientWidth > 1)
        .map((element) => element.textContent),
    ),
    `clipped labels at ${path}`,
  ).toEqual([]);
  expect(
    await page.getByTestId('language-selector').evaluateAll((selects) =>
      selects.flatMap((element) => {
        if (element.getBoundingClientRect().width === 0) return [];
        const select = element as HTMLSelectElement;
        const style = getComputedStyle(select);
        const context = document.createElement('canvas').getContext('2d')!;
        context.font = `${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
        const text = select.selectedOptions[0]?.textContent ?? '';
        const available = select.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
        return context.measureText(text).width > available + 1 ? [text] : [];
      }),
    ),
    `clipped selected language at ${path}`,
  ).toEqual([]);
  expect(
    await page.locator('.section-head').evaluateAll((heads) =>
      heads.flatMap((head) => {
        const title = head.querySelector('h2')?.getBoundingClientRect();
        const link = head.querySelector('a')?.getBoundingClientRect();
        return title && link && title.width > 0 && link.width > 0 &&
          title.left < link.right - 1 && title.right > link.left + 1 &&
          title.top < link.bottom - 1 && title.bottom > link.top + 1
          ? [head.textContent]
          : [];
      }),
    ),
    `overlapping section headings at ${path}`,
  ).toEqual([]);
  let screenshotPath: string | undefined;
  if (process.env.SCREENSHOT_DIR) {
    const directory = resolve(process.env.SCREENSHOT_DIR, info.project.name);
    mkdirSync(directory, { recursive: true });
    screenshotPath = resolve(directory, `ta-${name}.png`);
  }
  await info.attach(`ta-${name}`, {
    body: await page.screenshot({ fullPage: true, path: screenshotPath }),
    contentType: 'image/png',
  });
}

test('Tamil fonts, navigation, server pages and saved language render together', async ({
  page,
  context,
}, info) => {
  test.setTimeout(180_000);
  const base = String(info.project.use.baseURL);
  const tamilFonts: { url: string; status: number; type: string | undefined }[] = [];
  page.on('response', (response) => {
    if (/NotoSansTamil[^/]*\.ttf/.test(response.url()))
      tamilFonts.push({
        url: response.url(),
        status: response.status(),
        type: response.headers()['content-type'],
      });
  });
  await context.addCookies([{ name: 'glob2_locale', value: 'ta', url: base }]);
  await page.route('glob2://**', (route) => route.abort());
  for (const path of ['/', '/maps', '/account', '/map-studio', `/j/${INVITE_CODE}`, '/signin']) {
    await capture(page, info, path, path.replaceAll('/', '-') || 'home');
  }
  await expect(page.locator('#signin-username')).toBeVisible();
  expect(tamilFonts.length).toBeGreaterThan(0);
  for (const font of tamilFonts) {
    expect(font.status, font.url).toBe(200);
    expect(font.type, font.url).toContain('font/ttf');
  }
  await page.goto('/');
  await expect(page.locator('#hero-title')).toHaveText(catalog['Welcome to the colony']!);
  await expect(page.getByTestId('theme-toggle')).not.toHaveAttribute('aria-label', /Change theme/);
  const code = page.locator('input[name="code"]');
  await code.fill('no!');
  await code.press('Enter');
  await expect(code).toHaveAttribute('aria-invalid', 'true');
  await expect(page.locator('#join-hint')).not.toContainText('6 to 16 letters');
  await code.fill('ABCD');
  if (!(await page.getByTestId('language-selector').first().isVisible()))
    await page.locator('.rail-expand').click();
  const selector = page.locator('[data-testid="language-selector"]:visible').first();
  await selector.selectOption('en');
  await expect(code).toHaveValue('ABCD');
  await selector.selectOption('ta');
  await expect(code).toHaveValue('ABCD');
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('lang', 'ta');
  await expect(page.locator('#hero-title')).toHaveText(catalog['Welcome to the colony']!);
  const seed = (await (await page.request.get('/__seed')).json()) as { userSession: string };
  await context.addCookies([{ name: 'glob2_session', value: seed.userSession, url: base }]);
  for (const path of ['/account', '/building-studio', '/map-studio'])
    await capture(page, info, path, `registered-${path.slice(1)}`);
});
