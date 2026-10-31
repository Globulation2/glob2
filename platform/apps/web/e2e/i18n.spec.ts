import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { AI_TAGS } from '@glob2/protocol';
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
    if ((page.viewportSize()?.width ?? 1280) < 1100)
      await expect(page.locator('.app-sidebar [data-testid="language-selector"]')).toBeHidden();
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
    await page.evaluate(() => document.fonts.ready);
    if (locale.code === 'bn') {
      // Prove that the packaged Bengali face loaded, even on a host that also
      // happens to have a Bengali system font. Screenshots alone can hide that.
      const font = await page.evaluate(async () => {
        const faces = await document.fonts.load(
          '16px "Glob2 Scripts"',
          'ক্ষুদ্র শ্রমিকের প্রস্তুতি ১২',
        );
        const heading = document.querySelector('#hero-title');
        return {
          loaded: faces.length > 0 && faces.every((face) => face.status === 'loaded'),
          family: heading ? getComputedStyle(heading).fontFamily : '',
          bundled: performance
            .getEntriesByType('resource')
            .some((entry) => entry.name.includes('NotoSansBengali-Regular')),
        };
      });
      expect(font.loaded).toBe(true);
      expect(font.family).toContain('Glob2 Scripts');
      expect(font.bundled).toBe(true);
    }
    await expect(page.getByRole('link', { name: /Isles of Plenty/ })).toBeVisible();
    await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
    // Optional navigation (for example skins) follows instance feature flags.
    // Each translated page must retain the seeded instance's core destinations.
    for (const href of [
      '/',
      '/leaderboard',
      '/players',
      '/matches',
      '/maps',
      '/sets',
      '/ais',
      '/buildings',
      '/music',
    ])
      await expect(page.locator(`.app-sidebar .nav a[href="${href}"]`)).toHaveCount(1);
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

test('Bengali libraries, account and room invitations fit the viewport', async ({
  page,
  context,
}, info) => {
  await page.route('glob2://**', (route) => route.abort());
  await page.route('**/api/v1/accounts/me', (route) =>
    route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  );
  const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  await context.addCookies([{ name: 'glob2_locale', value: 'bn', url: base }]);
  const catalog = JSON.parse(
    readFileSync(new URL('../../../packages/i18n/locales/bn.json', import.meta.url), 'utf8'),
  ) as Record<string, string>;
  const translated = (source: string): string => {
    const value = catalog[source];
    if (!value) throw new Error(`Missing Bengali message: ${source}`);
    return value;
  };
  for (const [path, heading, name] of [
    ['/maps', 'Maps', 'maps'],
    ['/ais', 'AI Library', 'ais'],
    ['/buildings', 'Building library', 'buildings'],
    ['/sets', 'Terrain & resource sets', 'sets'],
    ['/account', 'Your account', 'account'],
    ['/signin', 'Sign in', 'signin'],
    ['/j/GLOBJOIN', 'You’re invited', 'invite'],
  ] as const) {
    await page.goto(path);
    await expect(page.locator('html')).toHaveAttribute('lang', 'bn');
    await expect(page.getByRole('heading', { level: 1 })).toHaveText(translated(heading));
    if (path === '/ais') {
      const tags = page.getByRole('group', { name: translated('AI tags'), exact: true });
      for (const tag of AI_TAGS)
        await expect(
          tags.getByRole('button', { name: translated(tag), exact: true }),
        ).toBeVisible();
    }
    const search = page.locator('main input[type="search"]').first();
    if (await search.count()) {
      await search.fill('নদী');
      await expect(search).toHaveValue('নদী');
    }
    await page.evaluate(() => document.fonts.ready);
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
      ),
    ).toBeLessThanOrEqual(1);
    const directory = process.env.SCREENSHOT_DIR
      ? resolve(process.env.SCREENSHOT_DIR, info.project.name)
      : undefined;
    if (directory) mkdirSync(directory, { recursive: true });
    await info.attach(`bn-${name}`, {
      body: await page.screenshot({
        fullPage: true,
        path: directory ? resolve(directory, `bn-${name}.png`) : undefined,
      }),
      contentType: 'image/png',
    });
  }
});

for (const language of ['fr', 'bn']) {
  test(`changing to ${language} updates the page without losing input`, async ({ page }) => {
    await page.route('**/api/v1/accounts/me', (route) =>
      route.fulfill({
        status: 401,
        json: { code: 'unauthenticated', message: 'Sign in required.' },
      }),
    );
    await page.goto('/');
    const code = page.locator('input[name="code"]');
    await code.fill('ABCD');
    if (!(await page.getByTestId('language-selector').first().isVisible()))
      await page.locator('.rail-expand').click();
    const selector = page.locator('[data-testid="language-selector"]:visible').first();
    await selector.selectOption(language);
    await expect(page.locator('html')).toHaveAttribute('lang', language);
    await expect(page.locator('html')).toHaveAttribute('dir', 'ltr');
    await expect(code).toHaveValue('ABCD');
    await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
    await page.reload();
    await expect(page.locator('html')).toHaveAttribute('lang', language);
  });
}
