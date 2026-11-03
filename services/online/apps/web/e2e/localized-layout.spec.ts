import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';

const messages = JSON.parse(
  readFileSync(new URL('../../../packages/i18n/locales/bg.json', import.meta.url), 'utf8'),
) as Record<string, string>;

test('long localized controls fit phone columns and preserve desktop layout', async ({
  page,
  request,
  baseURL,
}, info) => {
  test.setTimeout(240_000);
  test.skip(info.project.name !== 'desktop', 'Both viewports are exercised explicitly.');
  if (!baseURL) throw new Error('A seeded test server is required.');
  const seed = (await (await request.get('/__seed')).json()) as {
    userSession: string;
    mapId: string;
    accounts: { kestrel: string };
  };
  await page.context().addCookies([
    { name: 'glob2_locale', value: 'bg', url: baseURL },
    { name: 'glob2_session', value: seed.userSession, url: baseURL },
  ]);
  await page.goto('/building-studio');
  await page.getByRole('button', { name: messages['Create a family'], exact: true }).click();
  await expect(
    page.getByRole('heading', { name: messages['Building family editor'], exact: true }),
  ).toBeVisible();
  const editor = new URL(page.url()).pathname;
  const buildings = (await (await request.get('/api/v1/buildings')).json()) as {
    items: { id: string }[];
  };
  expect(buildings.items.length).toBeGreaterThan(0);
  const routes = [
    `/players/${seed.accounts.kestrel}`,
    `/maps/${seed.mapId}`,
    `/buildings/${buildings.items[0]?.id}`,
    '/account',
    '/maps/new',
    '/ais/new',
    '/generators/new',
    '/music/new',
    '/sets/new',
    editor,
  ];
  for (const width of [390, 1280]) {
    await page.setViewportSize({ width, height: 900 });
    for (const route of routes) {
      await test.step(`${width}px ${route}`, async () => {
        await page.goto(route, { waitUntil: 'networkidle' });
        await expect(page.getByRole('heading', { level: 1 }).first()).toBeVisible();
        await page.evaluate(() => document.fonts.ready);
        await expect(page.locator('html')).toHaveAttribute('lang', 'bg');
        if (route === '/sets/new') {
          const addLabel = (key: string) => {
            const template = messages['Add {slot0}'];
            const kind = messages[key];
            if (!template || !kind) throw new Error('Missing translated set-entry label');
            return template.replace('{slot0}', kind);
          };
          await expect(
            page.getByRole('button', { name: addLabel('Terrain'), exact: true }),
          ).toBeVisible();
          await page
            .locator('.set-tabs')
            .getByRole('button', { name: messages['Resources'], exact: true })
            .click();
          await expect(
            page.getByRole('button', { name: addLabel('Resource'), exact: true }),
          ).toBeVisible();
          await page
            .locator('.set-tabs')
            .getByRole('button', { name: messages['Terrain'], exact: true })
            .click();
        }
        expect(
          await page.evaluate(() => document.documentElement.scrollWidth - innerWidth),
        ).toBeLessThanOrEqual(1);
        // Technical identifiers can overflow their flex label and cover Remove
        // even when all control boxes remain inside the viewport.
        for (const label of await page.locator('.building-field > label').all()) {
          expect(
            await label.evaluate((element) => {
              const bounds = element.getBoundingClientRect();
              const text = document.createTreeWalker(element, NodeFilter.SHOW_TEXT);
              while (text.nextNode()) {
                if (!text.currentNode.textContent?.trim()) continue;
                const range = document.createRange();
                range.selectNodeContents(text.currentNode);
                if (
                  [...range.getClientRects()].some(
                    (rect) => rect.left < bounds.left - 1 || rect.right > bounds.right + 1,
                  )
                )
                  return false;
              }
              return true;
            }),
          ).toBe(true);
        }
        // The shell can conceal overflow, so verify actual interactive bounds too.
        for (const control of await page
          .locator('main button:visible, main input:visible, main select:visible')
          .all()) {
          const bounds = await control.boundingBox();
          expect(bounds && bounds.x >= 0 && bounds.x + bounds.width <= width + 1).toBe(true);
        }
      });
    }
  }
});

test('Bulgarian music mood labels fit each hit area in phone and embedded players', async ({
  page,
  baseURL,
}, info) => {
  if (!baseURL) throw new Error('A built web app is required.');
  await page.context().addCookies([{ name: 'glob2_locale', value: 'bg', url: baseURL }]);
  const id = '11111111-1111-4111-8111-111111111111';
  const release = {
    id,
    ownerId: id,
    metadata: {
      title: 'Authored soundtrack',
      artist: 'Fixture composer',
      description: '',
      license: 'CC0-1.0',
      credits: '',
      sources: [],
      tags: [],
      aiGenerated: false,
    },
    status: 'published',
    createdAt: '2026-10-01T12:00:00Z',
    frames: 480000,
    tracks: ['calm', 'building', 'combat'].map((mood) => ({
      mood,
      url: `/fixture-${mood}.opus`,
      sha256: 'a'.repeat(64),
      waveform: [0.2, 0.6, 0.4],
    })),
    warnings: [],
    inspection: null,
    uploaded: [],
    coverUrl: null,
    likes: 0,
    downloads: 0,
    liked: false,
    hidden: false,
    error: null,
  };
  await page.route(`**/api/v1/music/${id}`, (route) => route.fulfill({ json: release }));
  await page.goto(`/music/${id}`);
  await expect(
    page.getByRole('button', {
      name: messages['Crossfade to {value0}']?.replace('{value0}', messages.Build ?? ''),
      exact: true,
    }),
  ).toBeVisible();
  await page.evaluate(() => document.fonts.ready);
  for (const embedded of [false, true]) {
    if (embedded)
      await page.locator('.music-player').evaluate((element) => {
        element.style.width = '260px';
        element.style.maxWidth = '100%';
        element.style.boxSizing = 'border-box';
      });
    const bounds = await page.locator('.music-moods button').evaluateAll((buttons) =>
      buttons.map((button) => {
        const hit = button.getBoundingClientRect();
        const label = button.querySelector('strong');
        if (!label) throw new Error('Missing mood label');
        const range = document.createRange();
        range.selectNodeContents(label);
        return {
          text: label.textContent,
          hit: { left: hit.left, right: hit.right, height: hit.height },
          fits: [...range.getClientRects()].every(
            (rect) => rect.left >= hit.left - 1 && rect.right <= hit.right + 1,
          ),
        };
      }),
    );
    expect(bounds.map((bound) => bound.text)).toEqual([
      messages.Calm,
      messages.Build,
      messages.Combat,
    ]);
    expect(bounds.every((bound) => bound.fits && bound.hit.height >= 44)).toBe(true);
    const legend = await page.locator('.music-timeline').evaluate((element) => {
      const plot = element.querySelector('.music-waveforms')?.getBoundingClientRect();
      const seek = element.querySelector('.music-waveform-seek')?.getBoundingClientRect();
      if (!plot || !seek) throw new Error('Missing waveform or seek control');
      const labels = [...element.querySelectorAll('.music-waveform-labels span')].map((label) => {
        const range = document.createRange();
        range.selectNodeContents(label);
        return {
          text: label.textContent,
          right: Math.max(...[...range.getClientRects()].map((rect) => rect.right)),
        };
      });
      return {
        labels,
        plot: { left: plot.left, right: plot.right },
        seek: { width: seek.width, height: seek.height },
        panelRight: element.getBoundingClientRect().right,
      };
    });
    expect(legend.labels.every((label) => label.right <= legend.plot.left)).toBe(true);
    expect(legend.plot.right).toBeLessThanOrEqual(legend.panelRight + 1);
    expect(legend.seek.width).toBeGreaterThanOrEqual(44);
    expect(legend.seek.height).toBeGreaterThanOrEqual(44);
    expect(
      await page.evaluate(() => document.documentElement.scrollWidth - innerWidth),
    ).toBeLessThanOrEqual(1);
    await info.attach(`music-legend-bounds-${embedded ? 'embedded' : 'normal'}`, {
      body: Buffer.from(JSON.stringify(legend, null, 2)),
      contentType: 'application/json',
    });
    await info.attach(`music-legend-${embedded ? 'embedded' : 'normal'}`, {
      body: await page.locator('.music-timeline').screenshot(),
      contentType: 'image/png',
    });

    await info.attach(`music-mood-bounds-${embedded ? 'embedded' : 'normal'}`, {
      body: Buffer.from(JSON.stringify(bounds, null, 2)),
      contentType: 'application/json',
    });
    await info.attach(`music-moods-${embedded ? 'embedded' : 'normal'}`, {
      body: await page.locator('.music-moods').screenshot(),
      contentType: 'image/png',
    });
  }
});
