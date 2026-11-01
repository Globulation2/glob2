import { readFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { expect, test } from '@playwright/test';
import type { MapDetail } from '@glob2/protocol';

const detail = JSON.parse(
  readFileSync(
    new URL('../../../packages/protocol/fixtures/valid/MapDetail/public-map.json', import.meta.url),
    'utf8',
  ),
) as MapDetail;

test.beforeEach(async ({ page }) => {
  await page.route('**/api/v1/maps/*', (route) => route.fulfill({ json: detail }));
  await page.route('https://play.example.org/**/preview.webp', (route) =>
    route.fulfill({
      contentType: 'image/webp',
      body: readFileSync(join(import.meta.dirname, 'fixtures/maps/river.webp')),
    }),
  );
  await page.goto(`/maps/${detail.map.id}`);
  await expect(page.getByRole('button', { name: 'Reset view' })).toBeEnabled();
});

test('map preview wraps full periods, captures drags beyond its edge, and resets', async ({
  page,
}, info) => {
  const preview = page.getByRole('group', { name: `Preview of ${detail.map.title}` });
  await preview.focus();
  const screenshot = () =>
    preview.screenshot({
      style: '.map-preview-surface { outline: none !important; box-shadow: none !important; }',
    });
  const original = await screenshot();
  const bounds = await preview.boundingBox();
  if (!bounds) throw new Error('Preview has no bounds');
  const width = (await preview.evaluate((el: { clientWidth: number }) => el.clientWidth)) as number;
  const height = (await preview.evaluate(
    (el: { clientHeight: number }) => el.clientHeight,
  )) as number;
  const start = { x: bounds.x + bounds.width / 2, y: bounds.y + bounds.height / 2 };
  // A whole period must render the exact same pixels, including both seams.
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + width, start.y + height);
  await page.mouse.up();
  expect((await screenshot()).equals(original)).toBe(true);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(start.x + width * 0.25, start.y + height * 0.25, { steps: 8 });
  await page.mouse.up();
  expect((await screenshot()).equals(original)).toBe(false);
  const directory = process.env['SCREENSHOT_DIR'];
  if (directory) {
    mkdirSync(directory, { recursive: true });
    await page.screenshot({
      path: join(directory, `${info.project.name}-map-panned.png`),
      fullPage: true,
    });
  }
  await preview.press('Home');
  expect((await screenshot()).equals(original)).toBe(true);
  await preview.press('ArrowLeft');
  expect((await screenshot()).equals(original)).toBe(false);
  await page.getByRole('button', { name: 'Reset view' }).click();
  await preview.focus();
  expect((await screenshot()).equals(original)).toBe(true);
});

test('touch panning works at full-map scale and cancellation ends the drag', async ({
  page,
  context,
}, info) => {
  test.skip(info.project.name !== 'phone', 'Uses the Chromium touch device project.');
  const preview = page.getByRole('group', { name: `Preview of ${detail.map.title}` });
  const bounds = await preview.boundingBox();
  if (!bounds) throw new Error('Preview has no bounds');
  const session = await context.newCDPSession(page);
  const x = bounds.x + bounds.width / 2;
  const y = bounds.y + bounds.height / 2;
  await session.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y }] });
  await session.send('Input.dispatchTouchEvent', {
    type: 'touchMove',
    touchPoints: [{ x: x + 50, y: y - 50 }],
  });
  await expect(preview).toHaveClass(/dragging/);
  await session.send('Input.dispatchTouchEvent', { type: 'touchCancel', touchPoints: [] });
  await expect(preview).not.toHaveClass(/dragging/);
  await expect(preview.locator('.map-preview-tile').last()).not.toHaveCSS('left', '0px');
});

test('rectangular maps retain their proportions', async ({ page }) => {
  await page.route('https://play.example.org/**/preview.webp', (route) =>
    route.fulfill({
      contentType: 'image/svg+xml',
      body: '<svg xmlns="http://www.w3.org/2000/svg" width="512" height="256"><rect width="512" height="256" fill="green"/><rect width="128" height="128" fill="blue"/></svg>',
    }),
  );
  await page.reload();
  await expect(page.getByRole('button', { name: 'Reset view' })).toBeEnabled();
  const preview = page.getByRole('group', { name: `Preview of ${detail.map.title}` });
  const bounds = await preview.boundingBox();
  if (!bounds) throw new Error('Preview has no bounds');
  expect(bounds.width / bounds.height).toBeCloseTo(2, 1);
});
