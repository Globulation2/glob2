import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { locales } from '../../../packages/i18n/src/locales.ts';

for (const locale of locales) {
  test(`home loads and renders ${locale.code}`, async ({ page, context }, info) => {
    // A guest keeps the heading independent of the seeded API's current account.
    await page.route('**/api/v1/accounts/me', (route) =>
      route.fulfill({
        status: 401,
        json: { code: 'unauthenticated', message: 'Sign in required.' },
      }),
    );
    const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
    await context.addCookies([{ name: 'glob2_locale', value: locale.code, url: base }]);
    await page.goto('/');
    await expect(page.locator('html')).toHaveAttribute('lang', locale.code);
    await expect(page.locator('html')).toHaveAttribute('dir', locale.dir);
    await expect(page.getByTestId('language-selector').first()).toHaveValue(locale.code);
    const catalog = JSON.parse(
      readFileSync(
        new URL(`../../../packages/i18n/locales/${locale.code}.json`, import.meta.url),
        'utf8',
      ),
    ) as Record<string, string>;
    expect(catalog['Welcome to the colony']).toBeTruthy();
    const heading = catalog['Welcome to the colony'] ?? 'Welcome to the colony';
    if (locale.code !== 'en') expect(heading).not.toBe('Welcome to the colony');
    await expect(page.locator('#hero-title')).toHaveText(heading);
    await expect(page.getByRole('link', { name: /Isles of Plenty/ })).toBeVisible();
    await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
    await page.evaluate(() => document.fonts.ready);
    const statLabelsFit = await page
      .getByTestId('live-stats')
      .locator('.stat .k')
      .evaluateAll(
        (labels) =>
          labels.length === 3 &&
          labels.every((label) => {
            const parent = label.closest('.stat');
            if (!parent) return false;
            const card = parent.getBoundingClientRect();
            const text = document.createRange();
            text.selectNodeContents(label);
            return (
              label.scrollWidth <= label.clientWidth + 1 &&
              Array.from(text.getClientRects()).every(
                (rect) =>
                  rect.left >= card.left - 1 &&
                  rect.right <= card.right + 1 &&
                  rect.bottom <= card.bottom + 1,
              )
            );
          }),
      );
    expect(statLabelsFit).toBe(true);
    if (info.project.name === 'phone') {
      const title = await page.locator('#maps-title').boundingBox();
      const browse = await page
        .locator('#maps-title')
        .locator('..')
        .getByRole('link')
        .boundingBox();
      expect(title).not.toBeNull();
      expect(browse).not.toBeNull();
      if (!title || !browse)
        throw new Error('Map section heading and browse link must have bounds');
      expect(browse.y).toBeGreaterThanOrEqual(title.y + title.height - 1);
    }
    const signIn = page.locator('.app-sidebar .sidebar-account a[href="/signin"]');
    await expect(signIn).toBeVisible();
    expect(
      await signIn.evaluate((element) => element.scrollWidth - element.clientWidth),
    ).toBeLessThanOrEqual(1);
    // Long translations and RTL layout must fit the viewport on phones as well as desktops.
    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    );
    expect(overflow).toBeLessThanOrEqual(1);
    let screenshotPath: string | undefined;
    if (process.env.SCREENSHOT_DIR) {
      const directory = resolve(process.env.SCREENSHOT_DIR, info.project.name);
      mkdirSync(directory, { recursive: true });
      screenshotPath = resolve(directory, `${locale.code}-home.png`);
    }
    await info.attach(`${locale.code}-home`, {
      body: await page.screenshot({ fullPage: true, path: screenshotPath }),
      contentType: 'image/png',
    });
    await page.reload();
    await expect(page.locator('html')).toHaveAttribute('lang', locale.code);
  });
}

test('changing the selected language updates the page without losing input', async ({ page }) => {
  await page.route('**/api/v1/accounts/me', (route) =>
    route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  );
  await page.goto('/');
  const code = page.locator('input[name="code"]');
  await code.fill('ABCD');
  if (!(await page.getByTestId('language-selector').first().isVisible()))
    await page.locator('.rail-expand').click();
  const selector = page.locator('[data-testid="language-selector"]:visible').first();
  await selector.selectOption('fr');
  await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  await expect(code).toHaveValue('ABCD');
  await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
});

test.describe('Filipino browser preference', () => {
  test.use({ locale: 'tl-PH' });
  test('sign-in and saved choice agree', async ({ page, context }, info) => {
    const catalog = JSON.parse(
      readFileSync(new URL('../../../packages/i18n/locales/fil.json', import.meta.url), 'utf8'),
    ) as Record<string, string>;
    const captureDirectory = process.env.SCREENSHOT_DIR
      ? resolve(process.env.SCREENSHOT_DIR, info.project.name)
      : info.outputPath('captures');
    mkdirSync(captureDirectory, { recursive: true });
    await context.clearCookies();
    await page.route('**/api/v1/accounts/me', (route) =>
      route.fulfill({
        status: 401,
        json: { code: 'unauthenticated', message: 'Sign in required.' },
      }),
    );
    await page.goto('/signin');
    await expect(page.locator('html')).toHaveAttribute('lang', 'fil');
    await expect(page.locator('label[for="signin-username"]')).toContainText(
      catalog['Username'] ?? 'Username',
    );
    await expect(page.locator('label[for="signin-password"]')).toContainText(
      catalog['Password'] ?? 'Password',
    );
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
      ),
    ).toBeLessThanOrEqual(1);
    await info.attach('fil-signin', {
      body: await page.screenshot({
        fullPage: true,
        path: resolve(captureDirectory, 'fil-signin.png'),
      }),
      contentType: 'image/png',
    });

    await page.goto('/');
    await expect(page.locator('#hero-title')).toHaveText(
      catalog['Welcome to the colony'] ?? 'Welcome to the colony',
    );
    if (!(await page.getByTestId('language-selector').first().isVisible()))
      await page.locator('.rail-expand').click();
    const selector = page.locator('[data-testid="language-selector"]:visible').first();
    await selector.selectOption('en');
    await expect(page.locator('html')).toHaveAttribute('lang', 'en');
    await page.reload();
    await expect(page.locator('html')).toHaveAttribute('lang', 'en');
    if (!(await page.getByTestId('language-selector').first().isVisible()))
      await page.locator('.rail-expand').click();
    await page.locator('[data-testid="language-selector"]:visible').first().selectOption('fil');
    await expect(page.locator('html')).toHaveAttribute('lang', 'fil');
    await page.reload();
    await expect(page.locator('html')).toHaveAttribute('lang', 'fil');
    await page.goto('/maps');
    await expect(page.locator('html')).toHaveAttribute('lang', 'fil');
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
      ),
    ).toBeLessThanOrEqual(1);
    await info.attach('fil-maps', {
      body: await page.screenshot({
        fullPage: true,
        path: resolve(captureDirectory, 'fil-maps.png'),
      }),
      contentType: 'image/png',
    });
  });
});
