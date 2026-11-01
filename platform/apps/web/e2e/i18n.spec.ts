import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { AI_TAGS } from '@glob2/protocol';
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
    if ((page.viewportSize()?.width ?? 1280) < 1100)
      await expect(page.locator('.app-sidebar [data-testid="language-selector"]')).toBeHidden();
    if (locale.code === 'mr') {
      const selectorWasVisible =
        (await page.locator('[data-testid="language-selector"]:visible').count()) > 0;
      if (!selectorWasVisible) await page.locator('.rail-expand').click();
      await expect(page.locator('[data-testid="language-selector"]:visible').first()).toBeVisible();
      await page.evaluate(() => document.fonts.ready);
      const geometry = await page
        .locator('[data-testid="language-selector"]:visible')
        .first()
        .evaluate((element) => {
          const select = element as HTMLSelectElement;
          const style = getComputedStyle(select);
          const compact = select.parentElement?.querySelector('.language-preference-compact');
          if (!compact) throw new Error('Expected the compact language label.');
          const selected = select.selectedOptions[0];
          if (!selected) throw new Error('Expected a selected language option.');
          const compactText = compact.textContent;
          if (compactText === null) throw new Error('Expected compact language text.');
          const compactVisible = getComputedStyle(compact).display !== 'none';
          const label = compactVisible ? compactText.trim() : selected.text;
          const canvas = document.createElement('canvas');
          const context = canvas.getContext('2d');
          if (!context) throw new Error('Expected a 2D context for language text measurement.');
          context.font = compactVisible ? getComputedStyle(compact).font : style.font;
          const available =
            select.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
          return {
            label,
            fullName: selected.text,
            textWidth: context.measureText(label).width,
            available,
          };
        });
      expect(geometry.fullName).toBe('मराठी');
      expect(['मराठी', 'MR']).toContain(geometry.label);
      expect(geometry.textWidth).toBeLessThanOrEqual(geometry.available);
      if (!selectorWasVisible) {
        if (await page.locator('.navigation-drawer[open]').count())
          await page.locator('.navigation-drawer[open] .sidebar-close').click();
        else await page.locator('.app-sidebar .sidebar-toggle').click();
      }
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
      if ((page.viewportSize()?.width ?? 1280) < 1100) {
        await expect(page.locator('.app-sidebar .language-preference')).toBeHidden();
        await page.locator('.rail-expand').click();
        const selector = page.locator('.drawer-content [data-testid="language-selector"]');
        await selector.scrollIntoViewIfNeeded();
        await expect(selector).toBeVisible();
        await expect(selector.locator('option:checked')).toHaveText(locale.name);
        expect((await selector.boundingBox())?.width ?? 0).toBeGreaterThan(180);
        const directory = process.env.SCREENSHOT_DIR;
        const path = directory
          ? resolve(directory, info.project.name, 'te-language-drawer.png')
          : undefined;
        if (directory) mkdirSync(resolve(directory, info.project.name), { recursive: true });
        await info.attach('te-language-drawer', {
          body: await page.screenshot({ path }),
          contentType: 'image/png',
        });
        await page.locator('.sidebar-close').click();
      }
    }
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
    const instanceResponse = await page.request.get('/api/v1/instance');
    expect(instanceResponse.ok()).toBe(true);
    const instance = (await instanceResponse.json()) as { features?: string[] };
    // Retain exact destination coverage while respecting optional designer navigation.
    const destinations = [
      '/',
      '/leaderboard',
      '/players',
      '/matches',
      '/maps',
      '/sets',
      '/ais',
      '/buildings',
      '/music',
      ...(instance.features?.includes('skins.designer') ? ['/skins'] : []),
    ];
    // Core navigation must survive locale changes regardless of optional capabilities.
    expect(
      await page
        .locator('.app-sidebar .nav a')
        .evaluateAll((links) => links.map((link) => link.getAttribute('href'))),
    ).toEqual(destinations);

    const themeChangeLabel = catalog['{value0}. Change theme'];
    const defaultThemeLabel = catalog['Theme: same as this device'];
    if (!themeChangeLabel || !defaultThemeLabel) {
      throw new Error(`Missing theme labels for ${locale.code}`);
    }
    // The translation runtime isolates interpolated values in RTL messages.
    const interpolatedThemeLabel =
      locale.dir === 'rtl' ? `\u2068${defaultThemeLabel}\u2069` : defaultThemeLabel;
    await expect(page.getByTestId('theme-toggle')).toHaveAttribute(
      'aria-label',
      themeChangeLabel.replace('{value0}', interpolatedThemeLabel),
    );
    const signIn = page.locator('.app-sidebar .sidebar-account a[href="/signin"]');
    await expect(signIn).toBeVisible();
    expect(
      await signIn.evaluate((element) => element.scrollWidth - element.clientWidth),
    ).toBeLessThanOrEqual(1);
    if (locale.code === 'bn' && (page.viewportSize()?.width ?? 1280) < 1100) {
      // The narrow rail must use an icon, while retaining the full translated
      // action name for assistive technology and expanded navigation.
      const signInLabel = catalog['Sign in'];
      if (!signInLabel) throw new Error('Missing Bengali sign-in label');
      await expect(signIn).toHaveAccessibleName(signInLabel);
      const icon = signIn.locator('.sidebar-auth-icon');
      await expect(icon).toBeVisible();
      await expect(signIn.locator('.sidebar-auth-label')).toBeHidden();
      const buttonBounds = await signIn.boundingBox();
      const iconBounds = await icon.boundingBox();
      expect(buttonBounds).not.toBeNull();
      expect(iconBounds).not.toBeNull();
      if (!buttonBounds || !iconBounds) throw new Error('Missing sign-in icon geometry');
      expect(buttonBounds.width).toBeGreaterThanOrEqual(44);
      expect(buttonBounds.height).toBeGreaterThanOrEqual(44);
      expect(iconBounds.x).toBeGreaterThanOrEqual(buttonBounds.x + 4);
      expect(iconBounds.y).toBeGreaterThanOrEqual(buttonBounds.y + 4);
      expect(iconBounds.x + iconBounds.width).toBeLessThanOrEqual(
        buttonBounds.x + buttonBounds.width - 4,
      );
      expect(iconBounds.y + iconBounds.height).toBeLessThanOrEqual(
        buttonBounds.y + buttonBounds.height - 4,
      );
      await page.locator('.rail-expand').click();
      const navigation = page.locator('.navigation-drawer');
      await expect(navigation).toBeVisible();
      const expanded = navigation.locator('.sidebar-account a[href="/signin"]');
      await expect(expanded).toHaveAccessibleName(signInLabel);
      await expect(expanded.locator('.sidebar-auth-label')).toHaveText(signInLabel);
      await expect(expanded.locator('.sidebar-auth-label')).toBeVisible();
      await expect(expanded.locator('.sidebar-auth-icon')).toBeHidden();
      await page.keyboard.press('Escape');
      await expect(navigation).not.toBeVisible();
    }
    // Long translations and RTL layout must fit the viewport on phones as well as desktops.
    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    );
    expect(overflow).toBeLessThanOrEqual(1);
    await page.evaluate(() => document.fonts.ready);
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

for (const { locale, languageName, searchTerm } of [
  { locale: 'bn', languageName: 'Bengali', searchTerm: 'নদী' },
  { locale: 'fil', languageName: 'Filipino', searchTerm: 'ilog' },
] as const) {
  test(`${languageName} libraries, account and room invitations fit the viewport`, async ({
    page,
    context,
  }, info) => {
    await page.route('glob2://**', (route) => route.abort());
    await page.route('**/api/v1/accounts/me', (route) =>
      route.fulfill({
        status: 401,
        json: { code: 'unauthenticated', message: 'Sign in required.' },
      }),
    );
    const base = String(info.project.use.baseURL ?? 'http://127.0.0.1:4280');
    await context.addCookies([{ name: 'glob2_locale', value: locale, url: base }]);
    const catalog = JSON.parse(
      readFileSync(
        new URL(`../../../packages/i18n/locales/${locale}.json`, import.meta.url),
        'utf8',
      ),
    ) as Record<string, string>;
    const translated = (source: string): string => {
      const value = catalog[source];
      if (!value) throw new Error(`Missing ${languageName} message: ${source}`);
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
      await expect(page.locator('html')).toHaveAttribute('lang', locale);
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
        await search.fill(searchTerm);
        await expect(search).toHaveValue(searchTerm);
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
      await info.attach(`${locale}-${name}`, {
        body: await page.screenshot({
          fullPage: true,
          path: directory ? resolve(directory, `${locale}-${name}.png`) : undefined,
        }),
        contentType: 'image/png',
      });
    }
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

test('changing to Telugu and back preserves input', async ({ page }) => {
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
  const visibility = catalog['Unlisted: anyone with the link'];
  expect(visibility).toBeTruthy();
  await expect(page.locator('.upload-form option[value="unlisted"]')).toHaveText(visibility ?? '');

  // Exercise the actual long instruction after opening a real, locally enabled
  // designer. Checking text ranges catches clipping hidden by stage overflow.
  await page.goto('/skins');
  await page.getByRole('button', { name: catalog['New skin'] ?? '', exact: true }).click();
  const hint = page.locator('.skin-stage-hint');
  await hint.waitFor({ state: 'visible' });
  await expect(hint).toHaveText(
    catalog['Drag to paint · Rotate tool to turn · scroll or pinch to zoom'] ?? '',
  );
  await page.evaluate(() => document.fonts.ready);
  const bounds = await hint.evaluate((element) => {
    const stage = element.closest('.skin-stage');
    const pose = stage?.querySelector('.skin-pose-strip');
    if (!stage || !pose)
      throw new Error('The actual designer stage and pose controls are required');
    const rectangle = (r: DOMRect) => ({
      left: r.left,
      right: r.right,
      top: r.top,
      bottom: r.bottom,
    });
    const stageBox = stage.getBoundingClientRect();
    const hintBox = element.getBoundingClientRect();
    const poseBox = pose.getBoundingClientRect();
    const range = document.createRange();
    range.selectNodeContents(element);
    return {
      stage: rectangle(stageBox),
      text: [...range.getClientRects()].map(rectangle),
      fontSize: parseFloat(getComputedStyle(element).fontSize),
      overlap:
        Math.max(0, Math.min(hintBox.right, poseBox.right) - Math.max(hintBox.left, poseBox.left)) *
        Math.max(0, Math.min(hintBox.bottom, poseBox.bottom) - Math.max(hintBox.top, poseBox.top)),
    };
  });
  expect(bounds.text.length).toBeGreaterThan(0);
  expect(bounds.fontSize).toBeGreaterThanOrEqual(10);
  expect(bounds.overlap, 'The full hint must stay separate from pose controls').toBe(0);
  for (const row of bounds.text) {
    expect(row.left).toBeGreaterThanOrEqual(bounds.stage.left - 1);
    expect(row.right).toBeLessThanOrEqual(bounds.stage.right + 1);
    expect(row.top).toBeGreaterThanOrEqual(bounds.stage.top - 1);
    expect(row.bottom).toBeLessThanOrEqual(bounds.stage.bottom + 1);
  }
  const directory = process.env.SCREENSHOT_DIR;
  await info.attach('te-skin-hint', {
    body: await page.screenshot({
      fullPage: true,
      path: directory ? resolve(directory, info.project.name, 'te-skin-hint.png') : undefined,
    }),
    contentType: 'image/png',
  });
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
  await expect(page.locator('[data-testid="theme-toggle"]:visible').first()).toHaveAttribute(
    'aria-label',
    /थीम बदला$/,
  );
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
