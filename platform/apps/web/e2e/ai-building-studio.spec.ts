import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { AxeBuilder } from '@axe-core/playwright';
import type { SeededHistory } from '../../api/test/historySeed.ts';
const id = '11111111-1111-4111-8111-111111111111',
  draftId = '22222222-2222-4222-8222-222222222222',
  revision = '33333333-3333-4333-8333-333333333333';
test('building workspace supports revision-bound chat, previews, restoration and mobile panes', async ({
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
    namespace: id,
    experiments: [],
    sprites: [{ key: 'hospital', frames: [{ imageHash: 'a'.repeat(64), width: 64, height: 96 }] }],
    variants: [
      {
        key: 'b-' + id + '-hospital',
        properties: {
          width: 2,
          height: 2,
          hpInit: 300,
          hpMax: 300,
          maxUnitInside: 4,
          gameSprite: 'package:hospital',
          miniSpriteImage: -1,
        },
        semantics: {
          healing: { enabled: true, duration: 20, unitMask: 7 },
          constructionCost: { wood: 10 },
          assignmentLimit: 4,
        },
        presentation: { displayName: 'Mushroom hospital' },
      },
    ],
  };
  const writes: unknown[] = [];
  let deleted = false;
  let reference = false;
  await page.route('**/api/v1/ai-building-studio/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (route.request().method() === 'DELETE') {
      deleted = true;
      return route.fulfill({ status: 204 });
    }
    if (path.endsWith('/threads')) return route.fulfill({ json: { items: [] } });
    if (route.request().method() === 'POST') {
      if (path.endsWith('/references')) {
        reference = true;
        return route.fulfill({ json: { hash: 'b'.repeat(64) } });
      }
      writes.push(route.request().postDataJSON());
      if (path.endsWith('/turns') && writes.length === 1)
        return route.fulfill({ status: 409, json: { message: 'Draft revision changed.' } });
      return route.fulfill({ json: { id: 'accepted' } });
    }
    if (path.endsWith('/account'))
      return route.fulfill({ json: { enabled: true, available: 3, reserved: 0, packs: [] } });
    if (path.endsWith('/progress'))
      return route.fulfill({
        json: { requestId: 'old', stages: [], notes: [], checks: [], artifacts: [] },
      });
    if (path.endsWith('/threads/' + id))
      return route.fulfill({
        json: {
          id,
          title: 'Mushroom hospital',
          draftId,
          messages: [{ id: 'message', role: 'assistant', text: 'Created your hospital.' }],
          requests: [],
          references: reference
            ? [
                {
                  hash: 'b'.repeat(64),
                  url: 'data:image/png;base64,' + image.toString('base64'),
                  label: 'Reference',
                },
              ]
            : [],
          revisions: [
            {
              requestId: 'old',
              title: 'First version',
              applied: true,
              baseRevision: revision,
              package: {
                ...pack,
                variants: pack.variants.map((v) => ({
                  ...v,
                  properties: { ...v.properties, hpMax: 200 },
                })),
              },
              report: { valid: true },
            },
          ],
        },
      });
    return route.fulfill({ status: 404, json: { message: 'Missing' } });
  });
  await page.route('**/api/v1/building-drafts/' + draftId, (route) =>
    route.fulfill({ json: { id: draftId, revision, name: 'Hospital', package: pack } }),
  );
  // Portable runs use stock art. Local visual evidence may use a generated
  // sprite without committing provider output as a permanent test fixture.
  const image = readFileSync(
    resolve(process.env['BUILDING_PREVIEW_IMAGE'] ?? '../../../data/gfx/inn0b0.png'),
  );
  await page.route('**/api/v1/**/assets/**', (route) =>
    route.fulfill({ contentType: 'image/png', body: image }),
  );
  await page.goto('/ai-building-studio/' + id);
  await expect(
    page.getByRole('heading', { name: 'Mushroom hospital', exact: true }).first(),
  ).toBeVisible();
  await expect(page.getByRole('button', { name: /3 (Building|Terrain) credits/ })).toBeVisible();
  const prompt = page.getByLabel('Describe your creation or ask a question');
  await prompt.fill('Make only the hospital cheaper.');
  await page.getByRole('button', { name: 'Send', exact: true }).click();
  await expect(page.getByRole('alert')).toContainText('Draft revision changed.');
  await expect(prompt).toHaveValue('Make only the hospital cheaper.');
  await page.getByText('Reference images', { exact: true }).first().click();
  await page
    .getByLabel('Reference images', { exact: true })
    .setInputFiles({ name: 'reference.png', mimeType: 'image/png', buffer: image });
  await expect(page.getByLabel('Use reference')).toBeChecked();
  await page.getByRole('button', { name: 'Send', exact: true }).click();
  expect(writes[1]).toMatchObject({ expectedRevision: revision, references: ['b'.repeat(64)] });
  if (info.project.name === 'phone')
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await expect(page.getByText('Interior seats')).toBeVisible();
  await page.getByLabel('Compare with').selectOption('old');
  await expect(page.getByText('Current saved draft: 300')).toBeVisible();
  await page.getByRole('combobox', { name: 'View revision' }).selectOption('old');
  await expect(page.getByRole('link', { name: 'Export viewed revision' })).toHaveAttribute(
    'href',
    `/api/v1/ai-building-studio/threads/${id}/revisions/old/file`,
  );
  if (info.project.name !== 'phone')
    await expect(page.getByText(/Editing current saved draft/).first()).toBeVisible();
  await page.getByRole('combobox', { name: 'View revision' }).selectOption('');
  await page.getByRole('tab', { name: 'History', exact: true }).click();
  await page.getByText('Generated revisions', { exact: true }).click();
  page.once('dialog', (d) => d.accept());
  await page.getByRole('button', { name: 'Restore this revision' }).click();
  expect(writes.at(-1)).toEqual({ expectedRevision: revision });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
    true,
  );
  const directory = resolve(
    process.env['SCREENSHOT_DIR'] ?? '../../../artifacts/building-studio/screenshots',
  );
  mkdirSync(directory, { recursive: true });
  await page.emulateMedia({ reducedMotion: 'reduce' });
  for (const theme of ['light', 'dark']) {
    await page.evaluate((t) => document.documentElement.setAttribute('data-theme', t), theme);
    const axe = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa']).analyze();
    expect(axe.violations.filter((v) => v.impact === 'critical' || v.impact === 'serious')).toEqual(
      [],
    );
    await page.evaluate(async () => {
      window.scrollTo({ top: 0, behavior: 'instant' });
      await new Promise<void>((done) =>
        requestAnimationFrame(() => requestAnimationFrame(() => done())),
      );
    });
    await page.screenshot({
      path: resolve(directory, `${info.project.name}-${theme}.png`),
      fullPage: true,
    });
  }
  if (info.project.name === 'phone')
    await page.getByRole('tab', { name: 'Chat', exact: true }).first().click();
  await page.getByText('Project options', { exact: true }).click();
  await page.getByRole('button', { name: 'Delete project history', exact: true }).click();
  expect(deleted).toBe(false);
  await page.getByRole('button', { name: 'Keep history', exact: true }).click();
  expect(deleted).toBe(false);
  await page.getByRole('button', { name: 'Delete project history', exact: true }).click();
  await page.getByRole('button', { name: 'Delete history permanently', exact: true }).click();
  await expect(page).toHaveURL(/\/ai-building-studio$/);
  expect(deleted).toBe(true);
});
