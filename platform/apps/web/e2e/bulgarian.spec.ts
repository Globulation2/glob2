import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';

const catalog = () =>
  JSON.parse(
    readFileSync(new URL('../../../packages/i18n/locales/bg.json', import.meta.url), 'utf8'),
  ) as Record<string, string>;

test('Bulgarian loads its bundled font and persists an explicit language choice', async ({
  page,
  context,
}, info) => {
  const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  await context.addCookies([{ name: 'glob2_locale', value: 'bg', url: base }]);
  const fontResponse = page.waitForResponse(
    (response) => /sans-core[^/]*\.ttf/.test(response.url()) && response.status() === 200,
  );
  await page.goto('/');
  await fontResponse;
  await page.evaluate(() => document.fonts.ready);
  await expect(page.locator('html')).toHaveAttribute('lang', 'bg');
  await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
  await expect(page.locator('#hero-title')).toHaveText(catalog()['Welcome to the colony'] ?? '');
  const font = await page.locator('#hero-title').evaluate((element) => ({
    family: getComputedStyle(element).fontFamily,
    loaded: [...document.fonts].some(
      (face) => face.family.includes('Glob2 Bulgarian') && face.status === 'loaded',
    ),
  }));
  expect(font.family).toContain('Glob2 Bulgarian');
  expect(font.loaded).toBe(true);
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth - innerWidth),
  ).toBeLessThanOrEqual(1);
  await info.attach('bulgarian-home', {
    body: await page.screenshot({ fullPage: true }),
    contentType: 'image/png',
  });
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('lang', 'bg');
  await page.goto('/signin');
  await page.evaluate(() => document.fonts.ready);
  await expect(page.locator('html')).toHaveAttribute('lang', 'bg');
  await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
  const signInFont = await page.locator('body').evaluate((element) => ({
    family: getComputedStyle(element).fontFamily,
    loaded: [...document.fonts].some(
      (face) => face.family.includes('Glob2 Bulgarian') && face.status === 'loaded',
    ),
  }));
  expect(signInFont.family).toContain('Glob2 Bulgarian');
  expect(signInFont.loaded).toBe(true);
  await info.attach('bulgarian-signin', {
    body: await page.screenshot({ fullPage: true }),
    contentType: 'image/png',
  });
});
