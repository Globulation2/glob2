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
