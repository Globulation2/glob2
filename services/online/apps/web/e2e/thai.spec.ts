import { expect, test } from '@playwright/test';

test.use({ locale: 'th-TH' });

test('Thai auto-selection loads the bundled font and survives switching and refresh', async ({
  page,
}) => {
  await page.route('**/api/v1/accounts/me', (route) =>
    route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  );
  await page.goto('/');
  await expect(page.locator('html')).toHaveAttribute('lang', 'th');
  await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
  await page.evaluate(() => document.fonts.load('16px "Glob2 Scripts"', 'ผู้เล่นกำลังก่อสร้าง'));
  expect(
    await page.evaluate(() => document.fonts.check('16px "Glob2 Scripts"', 'ผู้เล่นกำลังก่อสร้าง')),
  ).toBe(true);
  expect(
    await page.evaluate(() =>
      [...document.fonts].some(
        (font) => font.family.includes('Glob2 Scripts') && font.status === 'loaded',
      ),
    ),
  ).toBe(true);
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    ),
  ).toBeLessThanOrEqual(1);
  const code = page.locator('input[name="code"]');
  await code.fill('THAI');
  if (!(await page.getByTestId('language-selector').first().isVisible()))
    await page.locator('.rail-expand').click();
  const selector = page.locator('[data-testid="language-selector"]:visible').first();
  await selector.selectOption('en');
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
  await selector.selectOption('th');
  await expect(page.locator('html')).toHaveAttribute('lang', 'th');
  await expect(code).toHaveValue('THAI');
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('lang', 'th');
});
