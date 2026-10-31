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
    if (locale.code === 'mr') {
      const geometry = await page
        .getByTestId('language-selector')
        .first()
        .evaluate((element) => {
          const select = element as HTMLSelectElement;
          const style = getComputedStyle(select);
          const compact = select.parentElement!.querySelector('.language-preference-compact')!;
          const compactVisible = getComputedStyle(compact).display !== 'none';
          const label = compactVisible
            ? compact.textContent!.trim()
            : select.selectedOptions[0]!.text;
          const canvas = document.createElement('canvas');
          const context = canvas.getContext('2d')!;
          context.font = compactVisible ? getComputedStyle(compact).font : style.font;
          const available =
            select.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
          return {
            label,
            fullName: select.selectedOptions[0]!.text,
            textWidth: context.measureText(label).width,
            available,
          };
        });
      expect(geometry.fullName).toBe('मराठी');
      expect(['मराठी', 'MR']).toContain(geometry.label);
      expect(geometry.textWidth).toBeLessThanOrEqual(geometry.available);
    }
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
    // Skins is an instance feature; required navigation stays available in every locale.
    expect(
      await page
        .locator('.app-sidebar .nav a')
        .evaluateAll((links) => links.map((link) => link.getAttribute('href'))),
    ).toEqual(
      expect.arrayContaining([
        '/',
        '/leaderboard',
        '/players',
        '/matches',
        '/maps',
        '/sets',
        '/ais',
        '/buildings',
        '/music',
      ]),
    );
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

// Exercise script-font loading and an explicit saved choice, beyond inventory smoke tests.
test('Marathi switching restores LTR, loads the bundled font and keeps the chosen locale', async ({
  page,
  context,
}, info) => {
  await page.route('**/api/v1/accounts/me', (route) =>
    route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  );
  const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  await context.addCookies([{ name: 'glob2_locale', value: 'ar', url: base }]);
  await page.goto('/');
  await expect(page.locator('html')).toHaveAttribute('dir', 'rtl');
  const code = page.locator('input[name="code"]');
  await code.fill('MARATHI');
  if (!(await page.getByTestId('language-selector').first().isVisible()))
    await page.locator('.rail-expand').click();
  await page.locator('[data-testid="language-selector"]:visible').first().selectOption('mr');
  await expect(page.locator('html')).toHaveAttribute('lang', 'mr');
  await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
  await expect(page.getByTestId('theme-toggle')).toHaveAttribute('aria-label', /थीम बदला$/);
  await expect(
    page.locator('[data-testid="language-selector"]:visible').last().locator('option:checked'),
  ).toHaveText('मराठी');
  await expect(code).toHaveValue('MARATHI');
  expect(
    await page.locator('#hero-title').evaluate((element) => getComputedStyle(element).fontFamily),
  ).toContain('Glob2 Scripts');
  const loaded = await page.evaluate(async () => {
    const faces = await document.fonts.load('20px "Glob2 Scripts"', 'मराठी ळ ऱ ऍ ऑ क्ष ज्ञ श्र');
    return faces.some(
      (face) => face.family.replaceAll(/["']/g, '') === 'Glob2 Scripts' && face.status === 'loaded',
    );
  });
  expect(loaded).toBe(true);
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('lang', 'mr');
  await expect(page.getByTestId('language-selector').first()).toHaveValue('mr');
});
