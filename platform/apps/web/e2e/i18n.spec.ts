import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
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
    // The guest fixture has no skins.designer feature or moderation role.
    // Assert destinations so a missing translated navigation item cannot pass.
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
    const themeChangeLabel = catalog['{value0}. Change theme'];
    const defaultThemeLabel = catalog['Theme: same as this device'];
    if (!themeChangeLabel || !defaultThemeLabel) {
      throw new Error(`Missing theme labels for ${locale.code}`);
    }
    await expect(page.getByTestId('theme-toggle')).toHaveAttribute(
      'aria-label',
      themeChangeLabel.replace('{value0}', defaultThemeLabel),
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
    if (locale.code === 'nb') {
      const inviteCode = page.locator('input[name="code"]');
      await expect(inviteCode).toHaveAttribute('placeholder', 'Kode eller lenke');
      await page.evaluate(() => document.fonts.ready);
      const metrics = await inviteCode.evaluate((element) => {
        const input = element as HTMLInputElement;
        const placeholderStyle = getComputedStyle(input, '::placeholder');
        const inputStyle = getComputedStyle(input);
        const canvas = document.createElement('canvas');
        const drawing = canvas.getContext('2d');
        if (!drawing) throw new Error('A 2D canvas is required to measure the placeholder');
        drawing.font = `${placeholderStyle.fontWeight} ${placeholderStyle.fontSize} ${placeholderStyle.fontFamily}`;
        const letterSpacing = Number.parseFloat(placeholderStyle.letterSpacing) || 0;
        return {
          placeholder: input.placeholder,
          font: drawing.font,
          textWidth:
            drawing.measureText(input.placeholder).width +
            Math.max(0, input.placeholder.length - 1) * letterSpacing,
          availableWidth:
            input.clientWidth -
            Number.parseFloat(inputStyle.paddingLeft) -
            Number.parseFloat(inputStyle.paddingRight),
        };
      });
      expect(metrics.textWidth).toBeLessThanOrEqual(metrics.availableWidth);
      await info.attach('nb-invite-placeholder-metrics', {
        body: Buffer.from(JSON.stringify(metrics, null, 2)),
        contentType: 'application/json',
      });
      if (process.env.SCREENSHOT_DIR) {
        const directory = resolve(process.env.SCREENSHOT_DIR, info.project.name);
        mkdirSync(directory, { recursive: true });
        writeFileSync(
          resolve(directory, 'nb-placeholder.json'),
          JSON.stringify(metrics, null, 2) + '\n',
        );
      }
    }
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

test('changing to Norwegian preserves input and persists across app and sign-in pages', async ({
  page,
}) => {
  await page.route('**/api/v1/accounts/me', (route) =>
    route.fulfill({ status: 401, json: { code: 'unauthenticated', message: 'Sign in required.' } }),
  );
  await page.goto('/');
  const code = page.locator('input[name="code"]');
  await code.fill('ABCD');
  if (!(await page.getByTestId('language-selector').first().isVisible()))
    await page.locator('.rail-expand').click();
  const selector = page.locator('[data-testid="language-selector"]:visible').first();
  await selector.selectOption('nb');
  await expect(page.locator('html')).toHaveAttribute('lang', 'nb');
  await expect(page.locator('#hero-title')).toHaveText('Velkommen til kolonien');
  await expect(code).toHaveValue('ABCD');
  await expect(page.locator('.app-sidebar .nav a[href="/maps"]')).toBeVisible();
  await page.reload();
  await expect(page.locator('html')).toHaveAttribute('lang', 'nb');
  await expect(page.getByTestId('language-selector').first()).toHaveValue('nb');
  // Server-rendered sign-in pages must use the preference saved by the SPA.
  await page.goto('/signin');
  await expect(page.locator('html')).toHaveAttribute('lang', 'nb');
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Logg inn');
});

for (const preference of ['nb-NO', 'no-NO', 'nn-NO']) {
  test.describe(`Norwegian browser preference ${preference}`, () => {
    test.use({ locale: preference });
    test('cold startup agrees with server-rendered sign-in', async ({ page, context }) => {
      // A fresh context has neither a locale cookie nor a downloaded catalog.
      expect(
        (await context.cookies()).find((cookie) => cookie.name === 'glob2_locale'),
      ).toBeUndefined();
      await page.route('**/api/v1/accounts/me', (route) =>
        route.fulfill({
          status: 401,
          json: { code: 'unauthenticated', message: 'Sign in required.' },
        }),
      );
      const expected = preference === 'nn-NO' ? 'en' : 'nb';
      await page.goto('/');
      await expect(page.locator('html')).toHaveAttribute('lang', expected);
      await expect(page.locator('#hero-title')).toHaveText(
        expected === 'nb' ? 'Velkommen til kolonien' : 'Welcome to the colony',
      );
      await page.goto('/signin');
      await expect(page.locator('html')).toHaveAttribute('lang', expected);
      await expect(page.getByRole('heading', { level: 1 })).toHaveText(
        expected === 'nb' ? 'Logg inn' : 'Sign in',
      );
      await expect(page.locator('#register-username')).toBeVisible();
    });
  });
}

test('Norwegian account instructions and long actions fit desktop and phone layouts', async ({
  page,
  context,
}, info) => {
  const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
  const seed = (await (await page.request.get('/__seed')).json()) as { userSession: string };
  await context.addCookies([
    { name: 'glob2_session', value: seed.userSession, url: base },
    { name: 'glob2_locale', value: 'nb', url: base },
  ]);
  await page.goto('/account');
  await expect(page.locator('html')).toHaveAttribute('lang', 'nb');
  await expect(page.getByRole('heading', { name: 'Last ned dataene mine' })).toBeVisible();
  await expect(page.locator('#delete-title')).toHaveText('Slett kontoen min');
  const deleteAction = page.getByRole('button', { name: 'Slett kontoen min for godt' });
  await expect(deleteAction).toBeVisible();
  await page.evaluate(() => document.fonts.ready);
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    ),
  ).toBeLessThanOrEqual(1);
  const clipped = await page
    .locator('.account-settings .card, .account-settings .btn, .account-settings button')
    .evaluateAll((elements) =>
      elements
        .filter((element) => element.scrollWidth - element.clientWidth > 1)
        .map((element) => element.textContent?.trim()),
    );
  expect(clipped).toEqual([]);
  let screenshotPath: string | undefined;
  if (process.env.SCREENSHOT_DIR) {
    const directory = resolve(process.env.SCREENSHOT_DIR, info.project.name);
    mkdirSync(directory, { recursive: true });
    screenshotPath = resolve(directory, 'nb-account.png');
  }
  await info.attach('nb-account', {
    body: await page.screenshot({ fullPage: true, path: screenshotPath }),
    contentType: 'image/png',
  });
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
