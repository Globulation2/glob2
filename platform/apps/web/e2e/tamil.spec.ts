import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test, type Page, type TestInfo } from '@playwright/test';
import { COLONY_SKIN_MATERIALS, SWARM_MESHES } from '@glob2/protocol';
import { INVITE_CODE } from './showcase.ts';

const catalog = JSON.parse(
  readFileSync(new URL('../../../packages/i18n/locales/ta.json', import.meta.url), 'utf8'),
) as Record<string, string>;
const welcome = catalog['Welcome to the colony'];
if (!welcome) throw new Error('Missing Tamil welcome heading');

async function capture(
  page: Page,
  info: TestInfo,
  path: string,
  name: string,
  ready?: () => Promise<void>,
) {
  await page.goto(path);
  await ready?.();
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
    await page
      .locator(
        '.ms-prompt-ideas button, .mu-prompt, .ms-credit-button, .mu-inspector-heading button, .studio-compose-actions button, .skin-tools button, .skin-models button, .skin-materials button, .skin-shape-grid button, .skin-tool-options > button',
      )
      .evaluateAll((elements) =>
        elements.flatMap((element) => {
          const bounds = element.getBoundingClientRect();
          if (bounds.width === 0 || bounds.height === 0) return [];
          const pane = element.closest('.studio-conversation')?.getBoundingClientRect();
          return element.scrollWidth > element.clientWidth + 1 ||
            bounds.right > document.documentElement.clientWidth + 1 ||
            (pane && (bounds.left < pane.left - 1 || bounds.right > pane.right + 1))
            ? [element.textContent]
            : [];
        }),
      ),
    `clipped studio controls at ${path}`,
  ).toEqual([]);
  expect(
    await page.locator('.skin-tools button').evaluateAll((buttons) => {
      const bounds = buttons.map((button) => button.getBoundingClientRect());
      return bounds.flatMap((a, index) =>
        bounds
          .slice(index + 1)
          .flatMap((b) =>
            a.width &&
            b.width &&
            a.left < b.right - 1 &&
            a.right > b.left + 1 &&
            a.top < b.bottom - 1 &&
            a.bottom > b.top + 1
              ? [index]
              : [],
          ),
      );
    }),
    `overlapping painter tools at ${path}`,
  ).toEqual([]);
  expect(
    await page.getByTestId('language-selector').evaluateAll((selects) =>
      selects.flatMap((element) => {
        if (element.getBoundingClientRect().width === 0) return [];
        const select = element as HTMLSelectElement;
        const style = getComputedStyle(select);
        const context = document.createElement('canvas').getContext('2d');
        if (!context) throw new Error('Canvas text measurement is unavailable');
        context.font = `${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
        const text = select.selectedOptions[0]?.textContent ?? '';
        const available =
          select.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
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
        return title &&
          link &&
          title.width > 0 &&
          link.width > 0 &&
          title.left < link.right - 1 &&
          title.right > link.left + 1 &&
          title.top < link.bottom - 1 &&
          title.bottom > link.top + 1
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
  await expect(page.locator('#hero-title')).toHaveText(welcome);
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
  await expect(page.locator('#hero-title')).toHaveText(welcome);
  const seed = (await (await page.request.get('/__seed')).json()) as { userSession: string };
  await context.addCookies([{ name: 'glob2_session', value: seed.userSession, url: base }]);
  for (const path of [
    '/account',
    '/building-studio',
    '/map-studio',
    '/generator-studio',
    '/music',
    '/music-studio',
    '/skins',
  ])
    await capture(page, info, path, `registered-${path.slice(1)}`);
});

test('Tamil studio initial interfaces render with deterministic enabled fixtures', async ({
  page,
  context,
}, info) => {
  // Eleven full navigations include font settling and actual painter controls.
  test.setTimeout(360_000);
  const base = String(info.project.use.baseURL);
  await context.addCookies([{ name: 'glob2_locale', value: 'ta', url: base }]);
  const text = (source: string) => {
    const value = catalog[source];
    if (typeof value !== 'string') throw new Error(`Missing Tamil fixture label: ${source}`);
    return value;
  };
  const features = [
    'map-studio',
    'music-studio',
    'generator-studio',
    'ai-studio',
    'terrain-studio',
    'ai-building-studio',
    'skins.designer',
  ];
  const account = {
    id: '11111111-1111-4111-8111-111111111111',
    displayName: 'Tamil studio fixture',
    kind: 'registered',
    createdAt: '2026-01-01T00:00:00Z',
    role: 'user',
    status: 'active',
    identities: [],
    entitlements: [],
  };
  let localSkinDesigner = false;
  const unexpected: string[] = [];
  const mockedReads: string[] = [];
  let navigationPath = '';
  const instanceReads: { path: string; url: string; features: string[] }[] = [];
  // Keep the seeded instance contract, explicitly enabling only these fixture features.
  const instanceResponse = await page.request.get('/api/v1/instance');
  expect(instanceResponse.ok()).toBe(true);
  const instance = await instanceResponse.json();
  // Context routes survive the COOP process swaps made by full studio navigations.
  await context.route('**/api/v1/instance', (route) => {
    instanceReads.push({
      path: navigationPath,
      url: route.request().url(),
      features: [...features],
    });
    return route.fulfill({
      headers: { 'x-tamil-studio-fixture': 'enabled' },
      json: { ...instance, features },
    });
  });
  await context.route('**/api/v1/accounts/me', (route) =>
    localSkinDesigner
      ? route.fulfill({ status: 401, json: { message: 'Sign in required.' } })
      : route.fulfill({ json: account }),
  );
  await context.route(
    /\/api\/v1\/(?:map|music|generator|ai|terrain|ai-building)-studio\//,
    (route) => {
      const request = route.request();
      const path = new URL(request.url()).pathname;
      // No fixture operation can dispatch generation, checkout, or any other mutation.
      if (request.method() !== 'GET') {
        unexpected.push(`${request.method()} ${path}`);
        return route.fulfill({ status: 405, json: { message: 'Fixture is read-only.' } });
      }
      mockedReads.push(path);
      if (path.endsWith('/account'))
        return route.fulfill({
          json: {
            enabled: true,
            model: 'Deterministic fixture',
            maxRequestCredits: 3,
            balance: 3,
            available: 3,
            reserved: 0,
            packs: [],
            usage: [],
            activeRequest: null,
            rate: { input: 1, cachedInput: 1, output: 1 },
          },
        });
      if (path.endsWith('/threads') || path.endsWith('/projects'))
        return route.fulfill({ json: { items: [] } });
      unexpected.push(`GET ${path}`);
      return route.fulfill({ status: 404, json: { message: 'No such fixture.' } });
    },
  );
  await context.route('**/api/v1/building-drafts', (route) => {
    if (route.request().method() === 'GET') return route.fulfill({ json: { items: [] } });
    unexpected.push(`${route.request().method()} building-drafts`);
    return route.fulfill({ status: 405, json: { message: 'Fixture is read-only.' } });
  });
  async function captureFixture(path: string, name: string, ready: () => Promise<void>) {
    navigationPath = path;
    const before = instanceReads.length;
    // Observe the actual navigation response: a missed interception must fail explicitly,
    // rather than appearing to be a slow or unavailable translated studio.
    const responsePromise = page.waitForResponse(
      (response) => new URL(response.url()).pathname === '/api/v1/instance',
    );
    await capture(page, info, path, name, async () => {
      const response = await responsePromise;
      const payload = await response.json();
      await info.attach(`tamil-instance-${path.slice(1)}`, {
        body: Buffer.from(
          JSON.stringify({
            path,
            status: response.status(),
            fixture: response.headers()['x-tamil-studio-fixture'] ?? null,
            features: payload.features,
            interceptedRequests: instanceReads.slice(before),
          }),
        ),
        contentType: 'application/json',
      });
      expect(response.ok(), `instance response at ${path}`).toBe(true);
      expect(response.headers()['x-tamil-studio-fixture'], `instance fixture at ${path}`).toBe(
        'enabled',
      );
      expect(payload.features, `enabled features at ${path}`).toEqual(features);
      expect(instanceReads.slice(before).some((read) => read.path === path)).toBe(true);
      await ready();
    });
  }
  const pricingMessages = new Map<string, string>([
    [
      '/map-studio',
      'Discussion spends no credits; 1 available Map credit is required. Creation requests build automatically · 1 credit on delivery.',
    ],
    [
      '/music-studio',
      'Discussion spends no credits; 1 available Music credit is required. Creation requests build automatically · 1 credit on delivery. Includes all three moods.',
    ],
    [
      '/terrain-studio',
      'Discussion spends no credits; 1 available Terrain credit is required. Creation requests build automatically · 1 credit on delivery.',
    ],
  ]);
  for (const [path, heading, input] of [
    ['/map-studio', 'AI Map Studio', 'Describe your map or discuss changes'],
    ['/music-studio', 'AI Music Studio', 'Your idea or next change'],
    ['/generator-studio', 'Generator Studio', 'Your first idea'],
    ['/ai-studio', 'AI Colony Studio', 'Your first idea'],
    ['/terrain-studio', 'AI Terrain Studio', 'Your first idea'],
    ['/ai-building-studio', 'AI Building Studio', 'Your first idea'],
  ] as const) {
    await captureFixture(path, `enabled-${path.slice(1)}`, async () => {
      await expect(page.getByRole('heading', { name: text(heading), exact: true })).toBeVisible();
      await expect(page.locator('.studio-workspace')).toBeVisible();
      await expect(page.locator('.studio-header button').filter({ hasText: /3\s/ })).toBeVisible();
      const prompt = page.getByRole('textbox', { name: text(input), exact: true });
      await expect(prompt).toBeEnabled();
      await prompt.fill(text('My colony'));
      await expect(page.locator('.studio-compose-actions button')).toHaveAttribute(
        'aria-disabled',
        'false',
      );
      await expect(
        page.getByRole('heading', { name: text('Unavailable'), exact: true }),
      ).toHaveCount(0);
      await expect(page.locator('.studio-compose-help')).not.toContainText(
        'Generation is unavailable',
      );
      const pricing = pricingMessages.get(path);
      if (pricing) await expect(page.locator('.studio-compose-help')).toContainText(text(pricing));
      if (path === '/map-studio')
        await expect(page.locator('.studio-edit-target')).toContainText(text('New map'));
      if (path === '/music-studio') {
        await expect(page.locator('.studio-edit-target')).toContainText(text('New soundtrack'));
        for (const title of [
          'Moss & morning light',
          'Little clockwork garden',
          'A waltz for the rain',
        ])
          await expect(page.locator('.mu-prompt').filter({ hasText: text(title) })).toBeVisible();
      }
      expect(mockedReads).toContain(`/api/v1${path}/account`);
    });
  }
  await captureFixture('/building-studio', 'enabled-building-studio', async () => {
    await expect(
      page.getByRole('heading', { name: text('Building Studio'), exact: true }),
    ).toBeVisible();
    await expect(
      page.getByRole('button', { name: text('Create a family'), exact: true }),
    ).toBeEnabled();
  });
  // The anonymous skin designer is the real local painting UI; no purchased skin,
  // server-side design creation, remote atlas fixture, or model provider is needed.
  localSkinDesigner = true;
  await captureFixture('/skins', 'enabled-skin-designer', async () => {
    const skinName = page.getByRole('textbox', { name: text('Skin name'), exact: true });
    await expect(skinName).toBeEnabled();
    await expect(skinName).toHaveValue(text('My colony'));
    await expect(
      page.getByRole('group', { name: text('Model to paint'), exact: true }).getByRole('button'),
    ).toHaveText(['Worker', 'Warrior', 'Explorer', 'Swarm'].map(text));
    await expect(page.getByRole('complementary', { name: text('Paint tools') })).toBeVisible();
    await expect(page.locator('.skin-stage > .skin-viewport')).toBeVisible();
  });
  async function showMaterials(bottom: boolean) {
    const tools = page.getByRole('complementary', { name: text('Paint tools') });
    const expand = tools.getByRole('button', { name: text('Expand toolbox'), exact: true });
    if (await expand.isVisible()) await expand.click();
    await tools
      .getByRole('group', { name: text('Brush paints'), exact: true })
      .getByRole('button', { name: text('Material'), exact: true })
      .click();
    const materials = tools.getByRole('radiogroup', { name: text('Material'), exact: true });
    await expect(materials.getByRole('radio')).toHaveCount(COLONY_SKIN_MATERIALS.length);
    for (const material of COLONY_SKIN_MATERIALS)
      await expect(
        materials.getByRole('radio', { name: text(material.name), exact: true }),
      ).toHaveCount(1);
    expect(
      await materials.getByRole('radio').evaluateAll((buttons) =>
        buttons.flatMap((button) => {
          const label = button.querySelector('span');
          if (!label) return ['Missing material label'];
          const cell = button.getBoundingClientRect();
          const text = label.getBoundingClientRect();
          return text.top < cell.top - 1 ||
            text.bottom > cell.bottom + 1 ||
            label.scrollWidth > label.clientWidth + 1
            ? [label.textContent]
            : [];
        }),
      ),
      'Material captions stay inside their swatches',
    ).toEqual([]);
    await page.locator('.skin-toolbox, .skin-tool-options').evaluateAll((elements) => {
      for (const element of elements) element.scrollTop = 0;
    });
    if (bottom)
      await page.locator('.skin-toolbox, .skin-tool-options').evaluateAll((elements) => {
        for (const element of elements) element.scrollTop = element.scrollHeight;
      });
  }
  await captureFixture('/skins', 'enabled-skin-materials', () => showMaterials(false));
  await captureFixture('/skins', 'enabled-skin-materials-bottom', () => showMaterials(true));
  await captureFixture('/skins', 'enabled-skin-shapes', async () => {
    await page
      .getByRole('group', { name: text('Model to paint'), exact: true })
      .getByRole('button', { name: text('Swarm'), exact: true })
      .click();
    await page.locator('.skin-shape-button').click();
    const shapes = page.getByRole('dialog', { name: text('Swarm shape'), exact: true });
    await expect(shapes.locator('.skin-shape-grid button')).toHaveCount(SWARM_MESHES.length);
    await expect(shapes.locator('.skin-shape-grid strong')).toHaveText(
      ['Classic', 'Crown', 'Clutch', 'Toadstool', 'Coral', 'Skep', 'Bloom'].map(text),
    );
  });
  expect(unexpected, 'Studio capture must not dispatch writes or unmocked studio reads').toEqual(
    [],
  );
  await info.attach('tamil-studio-fixture-scope', {
    body: Buffer.from(
      JSON.stringify({
        features,
        account: 'Fixed registered account; anonymous local skin painter',
        wallet: 'Enabled, 3 available credits, no pending request or purchase packs',
        threadsAndProjects: 'Explicit empty lists: initial UI only',
        mockedReads,
        instanceReads,
        interception:
          'Browser-context routes with response marker and per-navigation feature assertions',
        unexpected,
        limits:
          'No model dispatch, generated artifacts, paid operation, or production feature validation',
      }),
    ),
    contentType: 'application/json',
  });
});
