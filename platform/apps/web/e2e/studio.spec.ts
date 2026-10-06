// Conversation turns authorize at most one server-directed build.
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { AxeBuilder } from '@axe-core/playwright';
import { expect, test } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';
const base = `http://127.0.0.1:${process.env['PORT'] ?? 4280}`;
test('conversation and canvas fit desktop and phone', async ({ page, request }, info) => {
  await page.emulateMedia({ reducedMotion: 'reduce', colorScheme: 'dark' });
  // Snapshot routes do not stream. Keep this test's connection open; SSE recovery has its own tests.
  await page.addInitScript(() => {
    class SnapshotEvents {
      onopen?: () => void;
      constructor() {
        queueMicrotask(() => this.onopen?.());
      }
      close() {}
    }
    Object.defineProperty(globalThis, 'EventSource', { value: SnapshotEvents });
  });
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
  let shownVersions = versions;
  let shownMessages = messages;
  let available = 3;
  const writes: unknown[] = [];
  const writePaths: string[] = [];
  await page.route('**/api/v1/map-studio/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (route.request().method() === 'POST') {
      writes.push(route.request().postDataJSON());
      writePaths.push(path);
      await route.fulfill({ json: { id: 'next' } });
      return;
    }
    if (path.endsWith('/events')) {
      await route.fulfill({
        status: 200,
        contentType: 'text/event-stream',
        body: ': connected\n\n',
      });
      return;
    }
    if (path.endsWith('/progress')) {
      await route.fulfill({
        json: {
          requestId: path.split('/').at(-2),
          stages: [],
          artifacts: [],
          checks: [],
          historical: true,
        },
      });
      return;
    }
    if (path.endsWith('/account'))
      await route.fulfill({
        json: { enabled: true, available, reserved: available ? 0 : 1, packs: [], usage: [] },
      });
    else if (path.endsWith('/threads'))
      await route.fulfill({ json: { items: [{ id, title: 'Island country' }] } });
    else
      await route.fulfill({
        json: {
          id,
          title: 'Island country',
          messages: shownMessages,
          requests: shownVersions,
          history: {},
        },
      });
  });
  await page.goto(`/map-studio/${id}`);
  await expect(page.locator('.app-sidebar')).toBeVisible();
  await expect(
    page
      .getByRole('navigation', { name: 'Main', exact: true })
      .getByRole('link', { name: 'Maps', exact: true }),
  ).toHaveAttribute('aria-current', 'page');
  await expect(page.getByRole('button', { name: /Generate map/ })).toHaveCount(0);
  await expect(page.locator('.ms-header')).toHaveCSS('height', '56px');
  const versionsTab = page.getByRole('button', { name: /^Map/ });
  if (await versionsTab.isVisible()) await versionsTab.click();
  const screenshotDir = process.env['SCREENSHOT_DIR'];
  if (screenshotDir && info.project.name === 'desktop') {
    mkdirSync(screenshotDir, { recursive: true });
    const measurements = [];
    for (const [width, height] of [
      [1440, 900],
      [1366, 768],
      [390, 844],
    ] as const) {
      await page.setViewportSize({ width, height });
      await expect(page.locator('.ms-workspace-root')).toHaveCSS('height', `${height}px`);
      if (width < 900) {
        await page.getByRole('button', { name: 'Chat', exact: true }).click();
        await page.screenshot({ path: join(screenshotDir, `after-${width}x${height}-chat.png`) });
        await page.getByRole('button', { name: /^Map/ }).click();
      } else {
        const pane = await page.locator('.ms-conversation').boundingBox();
        const log = await page.getByRole('log').boundingBox();
        if (!log || !pane) throw new Error('Conversation layout missing');
        expect(log.height / pane.height).toBeGreaterThanOrEqual(0.6);
        const viewer = await page.locator('.ms-viewer-wrap').boundingBox();
        const image = await page.locator('.ms-map-image').boundingBox();
        if (!image || !viewer) throw new Error('Map image missing');
        expect(image.height / viewer.height).toBeGreaterThan(0.75);
      }
      await expect
        .poll(async () => {
          const image = await page.locator('.ms-map-image').boundingBox();
          const surface = await page.locator('.ms-map-surface').boundingBox();
          if (!image || !surface) return false;
          const expected = Math.min(surface.width - 24, surface.height - 66);
          return Math.abs(image.width - expected) < 2 && Math.abs(image.height - expected) < 2;
        })
        .toBe(true);
      measurements.push({
        width,
        height,
        conversation: await page.locator('.ms-conversation').boundingBox(),
        log: await page.getByRole('log', { includeHidden: true }).boundingBox(),
        canvas: await page.locator('.ms-map-surface').boundingBox(),
        image: await page.locator('.ms-map-image').boundingBox(),
      });
      await page.screenshot({ path: join(screenshotDir, `after-${width}x${height}.png`) });
    }
    writeFileSync(join(screenshotDir, 'after-layout.json'), JSON.stringify(measurements, null, 2));
    await page.setViewportSize({ width: 1280, height: 860 });
  }
  await page.getByLabel('Compare with version').selectOption(versions[0]?.id ?? '');
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
  const conversationTab = page.getByRole('button', { name: 'Chat', exact: true });
  if (await conversationTab.isVisible()) await conversationTab.click();
  await page
    .getByRole('textbox', { name: 'Describe your map or discuss changes' })
    .fill('Add a second walking bridge.');
  await expect(page.getByRole('button', { name: 'Send message' })).toBeEnabled();
  await page.getByRole('button', { name: 'Send message' }).click();
  await expect.poll(() => writes.length).toBe(1);
  expect(writes[0]).toMatchObject({
    text: 'Add a second walking bridge.',
    settings: { width: 256, height: 128, players: 4 },
    parent: versions[1]?.id,
  });
  expect(writePaths[0]).toBe(`/api/v1/map-studio/threads/${id}/turns`);
  const settings = page.locator('.ms-settings-popover > summary');
  await settings.click();
  await expect(page.getByRole('combobox', { name: 'Players', exact: true })).toBeVisible();
  await page.getByRole('combobox', { name: 'Players', exact: true }).focus();
  await page.keyboard.press('Escape');
  await expect(settings).toBeFocused();
  await expect(page.getByRole('combobox', { name: 'Players', exact: true })).toBeHidden();

  if (info.project.name === 'desktop') {
    const separator = page.getByRole('separator');
    await separator.focus();
    await page.keyboard.press('End');
    await expect(separator).toHaveAttribute('aria-valuenow', '65');
    await page.keyboard.press('Home');
    await expect(separator).toHaveAttribute('aria-valuenow', '35');
  }
  const mapTab = page.getByRole('button', { name: /^Map/ });
  if (await mapTab.isVisible()) await mapTab.click();
  await page.getByLabel('Inspect version').selectOption(versions[0]?.id ?? '');
  if (await conversationTab.isVisible()) await conversationTab.click();
  await expect(page.getByText('Editing version 1', { exact: true })).toBeVisible();
  await page.getByRole('textbox').fill('Add timber near the colonies');
  await page.getByRole('button', { name: 'Send message' }).click();
  await expect.poll(() => writes.length).toBe(2);
  expect(writes[1]).toMatchObject({
    text: 'Add timber near the colonies',
    parent: versions[0]?.id,
    settings: { width: 256, height: 128, players: 4 },
  });

  await settings.click();
  await page.getByRole('combobox', { name: 'Players', exact: true }).selectOption('6');
  await page.getByRole('combobox', { name: 'Players', exact: true }).press('Escape');
  await expect(
    page.locator('.ms-composer-tools').getByText('New map', { exact: true }),
  ).toBeVisible();

  available = 0;
  await page.reload();
  await expect(page.getByRole('button', { name: 'Send message' })).toBeDisabled();
  await expect(page.getByText(/An available credit is needed to chat or build/)).toBeVisible();

  for (const status of ['processing', 'failed', 'uncertain']) {
    shownVersions = [
      ...versions,
      {
        ...versions[1],
        id: '33333333-3333-4333-8333-333333333333',
        status,
        created_at: '2026-01-03T12:00:00Z',
        error: status === 'failed' ? 'A colony lacks accessible starter food or timber.' : null,
      } as (typeof versions)[number],
    ];
    available = status === 'failed' ? 3 : 0;
    await page.reload();
    if (status === 'failed') {
      if (info.project.name === 'phone') await mapTab.click();
      await page.getByRole('button', { name: 'Prepare retry' }).click();
      await expect(page.getByRole('textbox')).toBeFocused();
      await expect(page.getByRole('textbox')).toHaveValue(/Please try building.*starter food/);
      expect(writes).toHaveLength(2);
    } else {
      await expect(page.getByRole('button', { name: 'Send message' })).toBeDisabled();
      if (status === 'uncertain') {
        await expect(page.getByText('Awaiting provider outcome', { exact: true })).toBeVisible();
        await expect(page.getByRole('button', { name: 'Prepare retry' })).toHaveCount(0);
      } else await expect(page.getByText('Building', { exact: true })).toBeVisible();
    }
  }
  if (info.project.name === 'phone') {
    shownMessages = [
      ...messages,
      ...Array.from({ length: 12 }, (_, n) => ({
        id: `history-${n}`,
        role: 'assistant',
        text: 'Keep generous colony space, accessible wheat and timber, and wide walking routes between islands.',
        created_at: `2026-01-03T12:${String(n).padStart(2, '0')}:00Z`,
      })),
    ];
    await page.reload();
    const log = page.getByRole('log');
    await expect(page.getByText('Designer request failed')).toHaveCount(0);
    await page.getByRole('textbox').fill('Keep this draft while inspecting the map');
    await expect(
      page.getByText(
        'Keep generous colony space, accessible wheat and timber, and wide walking routes between islands.',
        { exact: true },
      ),
    ).toHaveCount(12);
    const top = await log.evaluate((el) => {
      el.scrollTop = 100;
      return el.scrollTop;
    });
    expect(top).toBeGreaterThan(0);
    await mapTab.click();
    await conversationTab.click();
    await expect(page.getByRole('textbox')).toHaveValue('Keep this draft while inspecting the map');
    expect(await log.evaluate((el) => el.scrollTop)).toBe(top);
  }
  shownVersions = [];
  shownMessages = [];
  available = 3;
  await page.reload();
  await expect(page.getByText('What will your world look like?')).toBeVisible();
  await page.getByRole('button', { name: /Create a ring of islands/ }).click();
  await expect(page.getByRole('textbox')).toHaveValue(
    'Create a ring of islands around a shared lagoon',
  );
  expect(writes).toHaveLength(2);
});
