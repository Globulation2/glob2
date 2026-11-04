import { expect, test } from '@playwright/test';
import { AxeBuilder } from '@axe-core/playwright';
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import type { AiInfo, MusicRelease, SetInfo } from '@glob2/protocol';

const id = '11111111-1111-4111-8111-111111111111';
const createdAt = '2026-10-01T12:00:00Z';
const ai = {
  id,
  name: 'Patient Gardener',
  description: 'A colony builder with a steady food supply.',
  tags: ['Economy', 'Defensive'],
  visibility: 'public',
  hidden: false,
  owner: { id, displayName: 'Maple' },
  createdAt,
  updatedAt: createdAt,
  likes: 42,
  downloads: 231,
  liked: false,
  favourited: false,
  latestVersion: {
    id,
    hash: 'a'.repeat(64),
    label: '1.2.0',
    notes: '',
    profile: 2,
    createdAt,
    downloads: 186,
    downloadUrl: '/api/v1/ais/' + id + '/versions/' + id + '/file',
    validations: [],
  },
} satisfies AiInfo;
const terrain = {
  id,
  title: 'Mossy woodland',
  description: 'Forest terrain and fruit for a thriving colony.',
  tags: [],
  owner: { id, displayName: 'Maple' },
  visibility: 'public',
  hidden: false,
  likes: 12,
  liked: false,
  downloads: 8,
  createdAt,
  updatedAt: createdAt,
  versions: [
    {
      id,
      hash: 'a'.repeat(64),
      label: '1.0',
      notes: '',
      license: 'CC0-1.0',
      credits: [],
      simVersion: 'test',
      minVersionMinor: 133,
      terrainCount: 3,
      resourceCount: 2,
      createdAt,
    },
  ],
} satisfies SetInfo;
const music = {
  id,
  ownerId: id,
  metadata: {
    title: 'Moss lantern',
    artist: 'Maple',
    description: 'Three gentle moods for your colony.',
    license: 'CC0-1.0',
    credits: '',
    sources: [],
    aiGenerated: false,
    tags: ['forest'],
  },
  status: 'published',
  createdAt,
  frames: 48000 * 60,
  tracks: [],
  warnings: [],
  inspection: null,
  uploaded: [],
  coverUrl: null,
  likes: 12,
  downloads: 4,
  liked: false,
  hidden: false,
  error: null,
} satisfies MusicRelease;

const libraries = [
  { path: '/maps', endpoint: '/api/v1/maps', title: 'Maps', art: 'explorationFlag' },
  { path: '/sets', endpoint: '/api/v1/sets', title: 'Terrain & resource sets', art: 'wood' },
  { path: '/ais', endpoint: '/api/v1/ais', title: 'AI Library', art: 'swarm' },
  { path: '/buildings', endpoint: '/api/v1/buildings', title: 'Building library', art: 'inn' },
  { path: '/music', endpoint: '/api/v1/music', title: 'Music for your colony', art: 'inn' },
  { path: '/skins', endpoint: '/api/v1/skins/collection', title: 'My skins', art: 'swarm' },
];

for (const library of libraries) {
  test(`${library.path} shares responsive gallery, feedback, and accessible controls`, async ({
    page,
    request,
    baseURL,
  }, info) => {
    if (!baseURL) throw new Error('Test server URL required.');
    const seed = (await (await request.get('/__seed')).json()) as { userSession: string };
    await page
      .context()
      .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
    let state: 'populated' | 'empty' | 'error' | 'loading' = 'populated';
    await page.route(`**${library.endpoint}*`, async (route) => {
      const url = new URL(route.request().url());
      if (url.pathname !== library.endpoint) return route.continue();
      if (state === 'error')
        return route.fulfill({
          status: 503,
          json: { message: 'Library temporarily unavailable.' },
        });
      if (state === 'loading') return; // keep request pending until navigation cancels it
      if (state === 'empty')
        return route.fulfill({
          json:
            library.path === '/skins'
              ? {
                  designs: [],
                  presets: [],
                  activeSkinId: null,
                  equippedVersionId: null,
                  canUseCustom: true,
                  activeArtworkStatus: 'ready',
                }
              : { items: [], next: null },
        });
      if (library.path === '/ais') return route.fulfill({ json: { items: [ai] } });
      if (library.path === '/sets') return route.fulfill({ json: { items: [terrain] } });
      if (library.path === '/music') return route.fulfill({ json: { items: [music], next: null } });
      return route.continue();
    });
    await page.route(`**/api/v1/sets/${id}/versions/${id}/preview`, (route) =>
      route.fulfill({
        contentType: 'image/png',
        path: resolve(import.meta.dirname, 'fixtures/maps/isles.png'),
      }),
    );
    const screenshots = process.env['SCREENSHOT_DIR']
      ? resolve(process.env['SCREENSHOT_DIR'])
      : info.outputDir;
    mkdirSync(screenshots, { recursive: true });
    const widths = info.project.name === 'desktop' ? [1470, 900] : [390];
    for (const theme of ['dark', 'light']) {
      for (const width of widths) {
        await page.setViewportSize({ width, height: 900 });
        await page.goto(library.path);
        await page.evaluate((theme) => {
          document.documentElement.dataset.theme = theme;
        }, theme);
        await expect(page.locator('.library-header h1')).toHaveText(library.title);
        await expect(page.locator('.library-card').first()).toBeVisible();
        await expect(page.locator('.library-header img')).toHaveAttribute('width', '56');
        await expect(
          page.locator('.library-header .library-actions').locator('a, button').first(),
        ).toHaveClass(/primary/);
        const header = await page.locator('.library-header').boundingBox();
        const wrap = await page.locator('main > .wrap').boundingBox();
        expect(header && wrap && header.x > wrap.x).toBe(true);
        expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(
          true,
        );
        // Check element bounds as well as document width (the shell hides overflow).
        for (const element of await page
          .locator('.library-field:visible, .library-card:visible, .library-header .btn:visible')
          .all()) {
          const bounds = await element.boundingBox();
          expect(bounds && bounds.x >= 0 && bounds.x + bounds.width <= width + 1).toBe(true);
        }
        for (const select of await page.locator('.library-field select').all()) {
          expect(
            await select.evaluate((element) => getComputedStyle(element).backgroundImage),
          ).not.toBe('none');
        }
        if (width !== 900) {
          expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
        }
        await page.screenshot({
          path: resolve(screenshots, `${library.path.slice(1)}-${theme}-${width}.png`),
          fullPage: true,
        });
        if (library.path === '/maps' && width !== 1470) {
          await page.getByText('More filters', { exact: true }).click();
          await expect(page.getByLabel('Made with')).toBeVisible();
          await page.screenshot({
            path: resolve(screenshots, `maps-${theme}-${width}-more-filters.png`),
            fullPage: true,
          });
          await page.getByText('More filters', { exact: true }).click();
        }
      }
    }
    for (const theme of ['dark', 'light']) {
      for (const nextState of ['empty', 'loading', 'error'] as const) {
        state = nextState;
        await page.goto(library.path);
        await page.evaluate((theme) => {
          document.documentElement.dataset.theme = theme;
        }, theme);
        if (state === 'empty') await expect(page.locator('.library-page .empty')).toBeVisible();
        if (state === 'loading')
          await expect(page.locator('.library-feedback .loading')).toBeVisible();
        if (state === 'error')
          await expect(page.locator('.library-results').getByRole('alert')).toBeVisible();
        await page.screenshot({
          path: resolve(
            screenshots,
            `${library.path.slice(1)}-${theme}-${widths.at(-1)}-${nextState}.png`,
          ),
          fullPage: true,
        });
        if (state === 'error') {
          state = 'populated';
          await page.getByRole('button', { name: 'Try again', exact: true }).click();
          await expect(page.locator('.library-card').first()).toBeVisible();
        }
      }
    }
  });
}

test('music filters apply immediately, Enter submits multiple text fields, and reset preserves My music', async ({
  page,
  request,
  baseURL,
}) => {
  if (!baseURL) throw new Error('Test server URL required.');
  const seed = (await (await request.get('/__seed')).json()) as { userSession: string };
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.route('**/api/v1/music?*', (route) =>
    route.fulfill({ json: { items: [], next: null } }),
  );
  await page.goto('/music');
  await page.getByRole('button', { name: 'My releases and uploads' }).click();
  await page.getByText('More filters', { exact: true }).click();
  const response = page.waitForRequest((r) => new URL(r.url()).searchParams.get('ai') === 'true');
  await page.getByLabel('AI disclosure').selectOption('true');
  await response;
  await expect(page.locator('.library-more-filters summary')).toHaveText('More filters (1)');
  await page.getByLabel('Tag', { exact: true }).fill('forest');
  const entered = page.waitForRequest(
    (r) =>
      new URL(r.url()).searchParams.get('q') === 'moss' &&
      new URL(r.url()).searchParams.get('tag') === 'forest',
  );
  await page.getByLabel('Search', { exact: true }).fill(' moss ');
  await page.getByLabel('Search', { exact: true }).press('Enter');
  await entered;
  const cleared = page.waitForRequest((r) => {
    const q = new URL(r.url()).searchParams;
    return q.get('mine') === '1' && q.get('q') === '' && q.get('ai') === '' && q.get('tag') === '';
  });
  await page.getByRole('button', { name: 'Clear filters', exact: true }).click();
  await cleared;
  await expect(page.getByRole('button', { name: 'My releases and uploads' })).toHaveAttribute(
    'aria-pressed',
    'true',
  );
});

for (const library of libraries.filter((library) => library.path !== '/skins')) {
  test(`${library.path} trims live search, flushes Enter, and clears filtered results`, async ({
    page,
  }) => {
    await page.route(`**${library.endpoint}*`, (route) =>
      route.fulfill({ json: { items: [], next: null } }),
    );
    await page.goto(library.path);
    const search = page.getByRole('searchbox');
    const searched = page.waitForRequest(
      (request) => new URL(request.url()).searchParams.get('q') === 'moss',
    );
    await search.fill(' moss ');
    await searched;
    const entered = page.waitForRequest(
      (request) => new URL(request.url()).searchParams.get('q') === 'forest',
    );
    await search.fill(' forest ');
    await search.press('Enter');
    await entered;
    await expect(page.locator('.library-page .empty')).toBeVisible();
    if (process.env['SCREENSHOT_DIR']) {
      for (const theme of ['dark', 'light']) {
        await page.evaluate((theme) => {
          document.documentElement.dataset.theme = theme;
        }, theme);
        await page.screenshot({
          path: resolve(
            process.env['SCREENSHOT_DIR'],
            `${library.path.slice(1)}-${theme}-${page.viewportSize()?.width}-filtered.png`,
          ),
          fullPage: true,
        });
      }
    }
    await page.getByRole('button', { name: 'Clear filters', exact: true }).click();
    await expect(search).toHaveValue('');
    await expect(search).toBeFocused();
  });
}
