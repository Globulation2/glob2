import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { locales } from '../../../packages/i18n/src/locales.ts';
import type { SeededHistory } from '../../api/test/historySeed.ts';
import { INVITE_CODE } from './showcase.ts';

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
    if (locale.code === 'te') {
      const font = await page.locator('#hero-title').evaluate(async (element) => {
        const loaded = await document.fonts.load('24px "Glob2 Scripts"', 'తెలుగు');
        await document.fonts.ready;
        return {
          family: getComputedStyle(element).fontFamily,
          faces: loaded.map((face) => ({ family: face.family, status: face.status })),
        };
      });
      expect(font.family).toContain('Glob2 Scripts');
      expect(font.faces).toEqual(
        expect.arrayContaining([expect.objectContaining({ status: 'loaded' })]),
      );
    }
    await expect(page.getByRole('link', { name: /Isles of Plenty/ })).toBeVisible();
    await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
    expect(
      await page
        .locator('.app-sidebar .nav a')
        .evaluateAll((links) => links.map((link) => link.getAttribute('href'))),
    ).toEqual([
      '/',
      '/leaderboard',
      '/players',
      '/matches',
      '/maps',
      '/sets',
      '/ais',
      '/buildings',
      '/music',
    ]);
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
  for (const language of ['fr', 'te', 'en']) {
    await selector.selectOption(language);
    await expect(page.locator('html')).toHaveAttribute('lang', language);
    await expect(code).toHaveValue('ABCD');
    await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
  }
});

test('Telugu renders shared online pages, authentication, invites and errors', async ({
  page,
  context,
  request,
}, info) => {
  const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await context.addCookies([{ name: 'glob2_locale', value: 'te', url: base }]);
  await page.route('glob2://**', (route) => route.abort());
  await page.emulateMedia({ reducedMotion: 'reduce' });
  const catalog = JSON.parse(
    readFileSync(new URL('../../../packages/i18n/locales/te.json', import.meta.url), 'utf8'),
  ) as Record<string, string>;
  const capture = async (path: string, name: string, heading?: string) => {
    const response = await page.goto(path);
    expect(response?.status()).toBeLessThan(500);
    await expect(page.locator('html')).toHaveAttribute('lang', 'te');
    if (heading) {
      const translated = catalog[heading];
      expect(translated).toBeTruthy();
      await expect(page.getByRole('heading', { level: 1 })).toHaveText(translated ?? '');
    }
    const loaded = await page.evaluate(async () => {
      const faces = await document.fonts.load('20px "Glob2 Scripts"', 'తెలుగు');
      await document.fonts.ready;
      return faces.some((face) => face.status === 'loaded');
    });
    expect(loaded).toBe(true);
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
      ),
      path,
    ).toBeLessThanOrEqual(1);
    const directory = process.env.SCREENSHOT_DIR;
    const screenshotPath = directory
      ? resolve(directory, info.project.name, `te-${name}.png`)
      : undefined;
    if (directory) mkdirSync(resolve(directory, info.project.name), { recursive: true });
    await info.attach(`te-${name}`, {
      body: await page.screenshot({ fullPage: true, path: screenshotPath }),
      contentType: 'image/png',
    });
  };
  await capture('/signin', 'signin');
  await expect(page.locator('#signin-username')).toBeVisible();
  await capture(`/j/${INVITE_CODE}`, 'invite');
  await expect(page.locator('.room')).toHaveText('Saturday night colony');
  await capture('/j/NOSUCHCODE', 'invite-error', 'Invite not found');
  await capture('/maps', 'maps');
  await expect(page.getByTestId('map-card').first()).toBeVisible();
  await context.addCookies([{ name: 'glob2_session', value: seed.userSession, url: base }]);
  await capture('/account', 'account', 'Your account');
  await expect(page.locator('main')).toContainText('Kestrel');
  await capture('/maps/new', 'upload', 'Upload a map');
  const visibility = catalog['Unlisted — accessible by link'];
  expect(visibility).toBeTruthy();
  await expect(page.locator('.upload-form option[value="unlisted"]')).toHaveText(visibility ?? '');
});
