// Paid actions remain explicit while the player discusses and compares map versions.
import { mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { AxeBuilder } from '@axe-core/playwright';
import { expect, test } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';
const base = `http://127.0.0.1:${process.env['PORT'] ?? 4280}`;
test('design conversation, private versions and explicit generation fit desktop and phone', async ({
  page,
  request,
}, info) => {
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page.context().addCookies([{ name: 'glob2_session', value: seed.userSession, url: base }]);
  const map = (await (await request.get(`/api/v1/maps/${seed.mapId}`)).json()) as {
    versions: { hash: string }[];
  };
  const id = '11111111-1111-4111-8111-111111111111';
  const versions = [1, 2].map((n) => ({
    id: `22222222-2222-4222-8222-22222222222${n}`,
    thread_id: id,
    kind: 'generate',
    status: 'ready',
    input: {
      settings: { width: 256, height: 128, players: 4 },
      brief: '',
      messages: [],
      pipelineVersion: 'test',
    },
    map_id: seed.mapId,
    map_hash:
      map.versions[0]?.hash ??
      (() => {
        throw new Error('Seeded map version missing');
      })(),
    charged: true,
    created_at: `2026-01-0${n}T12:00:00Z`,
  }));
  const messages = [
    {
      id: 'm1',
      role: 'user',
      text: 'An island country with broad paths and ponds near every home.',
      created_at: '2026-01-01T12:00:00Z',
    },
    {
      id: 'm2',
      role: 'assistant',
      text: 'We can keep the island landscape and give each colony fertile ground, timber and space for upgrades.',
      created_at: '2026-01-01T12:00:01Z',
    },
  ];
  const writes: unknown[] = [];
  await page.route('**/api/v1/map-studio/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (route.request().method() === 'POST') {
      writes.push(route.request().postDataJSON());
      await route.fulfill({ json: { id: 'next' } });
      return;
    }
    if (path.endsWith('/account'))
      await route.fulfill({
        json: { enabled: true, available: 3, reserved: 0, packs: [], usage: [] },
      });
    else if (path.endsWith('/threads'))
      await route.fulfill({ json: { items: [{ id, title: 'Island country' }] } });
    else
      await route.fulfill({
        json: { id, title: 'Island country', messages, requests: versions, history: {} },
      });
  });
  await page.goto(`/map-studio/${id}`);
  if ((page.viewportSize()?.width ?? 1280) < 900)
    await page.getByRole('button', { name: 'Open navigation' }).click();
  await expect(
    page
      .getByRole('navigation', { name: 'Main', exact: true })
      .getByRole('link', { name: 'AI Map Studio' }),
  ).toHaveAttribute('aria-current', 'page');
  if ((page.viewportSize()?.width ?? 1280) < 900) await page.keyboard.press('Escape');
  await expect(page.getByRole('button', { name: 'Generate — 1 credit' })).toBeVisible();
  const versionsTab = page.getByRole('button', { name: 'Versions (2)' });
  if (await versionsTab.isVisible()) await versionsTab.click();
  await expect(page.getByRole('button', { name: 'Publish this version' })).toHaveCount(2);
  await page.getByLabel('Compare', { exact: true }).first().check();
  await page.getByLabel('Compare', { exact: true }).last().check();
  await expect(page.getByRole('region', { name: 'Map comparison' })).toBeVisible();
  const violations = (await new AxeBuilder({ page }).analyze()).violations.map((v) => ({
    id: v.id,
    nodes: v.nodes.map((n) => n.target),
  }));
  expect(violations).toEqual([]);
  expect(
    await page.evaluate(
      () =>
        (
          globalThis as unknown as {
            document: { documentElement: { scrollWidth: number; clientWidth: number } };
          }
        ).document.documentElement.scrollWidth -
        (
          globalThis as unknown as {
            document: { documentElement: { scrollWidth: number; clientWidth: number } };
          }
        ).document.documentElement.clientWidth,
    ),
  ).toBe(0);
  const directory = process.env['SCREENSHOT_DIR'];
  if (directory) {
    mkdirSync(directory, { recursive: true });
    await page.screenshot({
      path: join(directory, `${info.project.name}-map-studio.png`),
      fullPage: true,
    });
  }
  const conversationTab = page.getByRole('button', { name: 'Conversation', exact: true });
  if (await conversationTab.isVisible()) await conversationTab.click();
  await page
    .getByRole('textbox', { name: 'Describe your map or discuss changes' })
    .fill('Add a second walking bridge.');
  await expect(page.getByRole('button', { name: 'Generate — 1 credit' })).toBeDisabled();
  await page.getByRole('button', { name: 'Send message' }).click();
  await expect.poll(() => writes.length).toBe(1);
  expect(writes[0]).toMatchObject({ text: 'Add a second walking bridge.' });
});
