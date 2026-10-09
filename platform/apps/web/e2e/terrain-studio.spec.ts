import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { AxeBuilder } from '@axe-core/playwright';
import type { SeededHistory } from '../../api/test/historySeed.ts';
const id = '11111111-1111-4111-8111-111111111111',
  draftId = '22222222-2222-4222-8222-222222222222';
test('terrain creation workspace fits desktop and phone and submits one revision-bound turn', async ({
  page,
  request,
}, info) => {
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page.context().addCookies([
    {
      name: 'glob2_session',
      value: seed.userSession,
      url: `http://127.0.0.1:${process.env['PORT'] ?? 4280}`,
    },
  ]);
  await page.addInitScript(() => {
    class Events {
      onopen?: () => void;
      constructor() {
        queueMicrotask(() => this.onopen?.());
      }
      close() {}
      addEventListener() {}
    }
    Object.defineProperty(globalThis, 'EventSource', { value: Events });
  });
  const pack = {
    schemaVersion: 1,
    setId: id,
    versionId: draftId,
    title: 'Fungal swamp',
    description: 'A damp forest with renewable mushroom food.',
    tags: ['swamp'],
    license: 'CC-BY-4.0',
    credits: [{ author: 'Creator', license: 'CC-BY-4.0' }],
    terrains: [
      {
        key: 's:marsh',
        name: 'Fungal marsh',
        base: 'marsh',
        properties: { groundSpeedQ8: 128, growthQ8: 320 },
      },
    ],
    resources: [
      {
        key: 's:mushrooms',
        properties: {
          primaryMaterial: 'food',
          growthRate: 196608,
          spreadRate: 65536,
          farmable: true,
        },
        yields: { food: { capacity: 5, initial: 1, consumption: 'one' } },
        presentation: {
          name: 'Mushrooms',
          sprite: 'data/gfx/ressource',
          minimap: [70, 110, 60],
          levels: [{ stock: 0, variants: [{ frame: 10, weight: 1 }] }],
        },
      },
    ],
    assets: { schemaVersion: 1, sheets: [], terrains: {}, credits: [] },
  };
  const writes: unknown[] = [];
  let draftRevision = 1;
  const referenceHash = 'a'.repeat(64);
  let hasReference = false;
  await page.route('**/api/v1/terrain-studio/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (route.request().method() === 'POST') {
      if (path.endsWith('/references')) {
        hasReference = true;
        return route.fulfill({ json: { hash: referenceHash } });
      }
      writes.push(route.request().postDataJSON());
      if (writes.length === 1)
        return route.fulfill({ status: 409, json: { message: 'Draft revision changed.' } });
      if (writes.length === 2)
        return route.fulfill({ status: 503, json: { message: 'Connection interrupted.' } });
      await route.fulfill({ json: { id: 'next' } });
      return;
    }
    if (path.endsWith('/account'))
      return route.fulfill({ json: { enabled: true, available: 3, reserved: 0, packs: [] } });
    if (path.endsWith('/progress'))
      return route.fulfill({
        json: {
          requestId: 'generated',
          stages: ['prepare', 'artwork', 'assemble', 'checks', 'ready'].map((id) => ({
            id,
            label: {
              prepare: 'Design the set',
              artwork: 'Create artwork',
              assemble: 'Assemble the pack',
              checks: 'Validate and preview',
              ready: 'Ready to use',
            }[id],
            status: 'complete',
          })),
          checks: [
            {
              id: 'engine',
              label: 'Engine import and artwork',
              status: 'pass',
              detail: 'Accepted by the engine.',
            },
          ],
          notes: [],
          artifacts: [],
          historical: false,
        },
      });
    return route.fulfill({
      json: {
        id,
        title: 'Fungal swamp',
        draftId,
        cursor: '0',
        references: hasReference
          ? [
              {
                hash: referenceHash,
                label: 'Style reference',
                url: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=',
              },
            ]
          : [],
        revisions: [],
        messages: [
          {
            id: 'user',
            role: 'user',
            text: 'A fungal swamp with slow marsh and renewable mushroom food.',
          },
          {
            id: 'designer',
            role: 'assistant',
            text: 'The marsh slows ground movement. Mushrooms provide renewable food and can be farmed.',
          },
        ],
        requests: [{ id: 'generated', kind: 'generate', status: 'ready', charged: true }],
      },
    });
  });
  await page.route('**/api/v1/set-drafts/' + draftId, (route) =>
    route.fulfill({
      json: {
        id: draftId,
        revision: draftRevision,
        package: { ...pack, title: draftRevision === 1 ? pack.title : 'Saved elsewhere' },
        publishedVersionId: null,
        validation: { status: 'valid' },
      },
    }),
  );
  await page.goto('/terrain-studio/' + id);
  await expect(page.getByRole('heading', { name: 'Fungal swamp', exact: true })).toBeVisible();
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await expect(page.getByRole('heading', { name: 'Scene and resource gallery' })).toBeVisible();
  if (info.project.name === 'phone')
    await page.getByRole('tab', { name: 'Chat', exact: true }).click();
  await expect(page.getByRole('button', { name: /3 (Building|Terrain) credits/ })).toBeVisible();
  const width = await page.evaluate(() => ({
    content: document.documentElement.scrollWidth,
    viewport: innerWidth,
  }));
  expect(width.content).toBeLessThanOrEqual(width.viewport + 1);
  await page
    .getByLabel('Describe your creation or ask a question')
    .fill('Make only the marsh faster.');
  await page.getByRole('button', { name: 'Send', exact: true }).click();
  expect(writes).toHaveLength(1);
  await expect(page.getByRole('alert')).toContainText('Draft revision changed.');
  const prompt = page.getByLabel('Describe your creation or ask a question');
  await expect(prompt).toBeEnabled();
  await expect(prompt).toHaveValue('Make only the marsh faster.');
  // Uploading identical artwork must not create duplicate IDs in the turn.
  const reference = {
    name: 'reference.png',
    mimeType: 'image/png',
    buffer: Buffer.from('fixture'),
  };
  await page.getByText('Reference images', { exact: true }).first().click();
  await page.getByLabel('Reference images', { exact: true }).setInputFiles(reference);
  await expect(page.getByLabel('Use reference')).toBeChecked();
  await page.getByLabel('Reference images', { exact: true }).setInputFiles(reference);
  await expect(page.getByRole('button', { name: 'Send', exact: true })).toBeEnabled();
  await page.getByRole('button', { name: 'Send', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Retry saved request' })).toBeEnabled();
  await expect(prompt).toBeDisabled();
  await page.getByRole('button', { name: 'Retry saved request' }).click();
  expect(writes).toHaveLength(3);
  expect(writes[2]).toEqual(writes[1]);
  expect(writes[0]).toMatchObject({
    text: 'Make only the marsh faster.',
    expectedRevision: 1,
    references: [],
  });
  expect(writes[2]).toMatchObject({ references: [referenceHash] });
  const directory = resolve(
    process.env['SCREENSHOT_DIR'] ?? '../../../artifacts/terrain-studio/screenshots',
  );
  mkdirSync(directory, { recursive: true });
  await page.emulateMedia({ reducedMotion: 'reduce' });
  for (const theme of ['light', 'dark']) {
    await page.evaluate(
      (theme) => document.documentElement.setAttribute('data-theme', theme),
      theme,
    );
    const axe = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa']).analyze();
    expect(axe.violations.filter((v) => v.impact === 'critical' || v.impact === 'serious')).toEqual(
      [],
    );
    await page.screenshot({
      path: resolve(directory, `${info.project.name}-${theme}.png`),
      fullPage: true,
    });
  }
  await page.screenshot({ path: resolve(directory, `${info.project.name}.png`), fullPage: true });
  // A background refresh must preserve unsaved inspector values from this tab.
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await page.getByRole('tab', { name: 'Edit', exact: true }).click();
  const titleInput = page.getByLabel('Title', { exact: true });
  await titleInput.fill('My unsaved terrain title');
  draftRevision = 2;
  await expect(page.getByRole('alert')).toContainText('Another session saved revision 2', {
    timeout: 15000,
  });
  await expect(titleInput).toHaveValue('My unsaved terrain title');
  await page.getByRole('button', { name: 'Discard manual edits and load saved revision' }).click();
  await expect(titleInput).toHaveValue('Saved elsewhere');
});
