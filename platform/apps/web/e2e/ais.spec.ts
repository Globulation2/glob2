import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { AxeBuilder } from '@axe-core/playwright';
import { pendingAiReport } from '@glob2/protocol';
const id = '11111111-1111-4111-8111-111111111111',
  vId = '22222222-2222-4222-8222-222222222222';
const report = pendingAiReport('a'.repeat(64), '133-54-' + 'b'.repeat(64));
report.valid = true;
report.checks.forEach((c) => (c.status = 'passed'));
report.metadata = {
  apiVersion: 2,
  name: 'Patient Gardener',
  description: 'An economic AI',
  version: '1.2.0',
  author: 'Maple',
};
const version = {
  id: vId,
  hash: report.sourceHash,
  label: '1.2.0',
  notes: 'A steadier food supply and faster recovery.',
  profile: 2,
  createdAt: '2026-10-04T12:00:00Z',
  downloads: 186,
  downloadUrl: '/api/v1/ais/' + id + '/versions/' + vId + '/file',
  validations: [report],
};
const ai = {
  id,
  name: 'Patient Gardener',
  description:
    'A thoughtful colony builder that puts a reliable food supply first. Give it room to grow and watch a patient economy become a formidable opponent.',
  tags: ['Economy', 'Defensive'],
  visibility: 'public',
  hidden: false,
  owner: { id, displayName: 'Maple' },
  createdAt: version.createdAt,
  updatedAt: version.createdAt,
  likes: 42,
  downloads: 231,
  latestVersion: version,
  liked: false,
  favourited: false,
};
test('AI discovery, version details, social actions and gated publishing', async ({
  page,
}, testInfo) => {
  let active = false;
  await page.route('**/api/v1/instance', (r) =>
    r.fulfill({
      json: {
        name: 'Glob2 Online',
        origin: 'http://127.0.0.1:4287',
        realtimeUrl: 'ws://127.0.0.1:4287/realtime',
        supportedSimVersions: [],
        authProviders: [],
        queues: [],
        guestsAllowed: true,
      },
    }),
  );
  await page.route('**/api/v1/accounts/*/avatar', (r) => r.fulfill({ status: 404 }));
  await page.route('**/api/v1/accounts/me', (r) =>
    r.fulfill({
      json: {
        id,
        displayName: 'Maple',
        kind: 'registered',
        createdAt: version.createdAt,
        role: 'user',
        status: 'active',
        identities: [],
        entitlements: [],
      },
    }),
  );
  await page.route('**/api/v1/ais**', async (r) => {
    const url = new URL(r.request().url());
    if (url.pathname.endsWith('/like') || url.pathname.endsWith('/favourite')) {
      active = !active;
      await r.fulfill({ json: { active, likes: 43 } });
    } else if (url.pathname === '/api/v1/ais')
      await r.fulfill({
        json: {
          items: [
            { ...ai, favourited: active },
            {
              ...ai,
              id: '33333333-3333-4333-8333-333333333333',
              name: 'Copper Swarm',
              tags: ['Rush', 'Expansion'],
              description: 'An energetic expansion strategy that keeps your colony on its toes.',
              likes: 28,
            },
            {
              ...ai,
              id: '44444444-4444-4444-8444-444444444444',
              name: 'Quiet Frontier',
              tags: ['Balanced'],
              description: 'A balanced opponent for learning and experimenting with local games.',
              likes: 17,
            },
          ],
        },
      });
    else
      await r.fulfill({
        json: {
          ai: { ...ai, favourited: active },
          versions: [
            version,
            {
              ...version,
              id: 'old-release',
              label: '1.1.0',
              downloadUrl: '/api/v1/ais/' + id + '/versions/old-release/file',
              downloads: 45,
            },
          ],
          viewer: { owner: true, moderator: false },
        },
      });
  });
  await page.route('**/api/v1/ai-uploads', (r) =>
    r.fulfill({
      json: {
        id,
        sourceHash: report.sourceHash,
        status: 'valid',
        report,
        expiresAt: '2099-01-01T00:00:00Z',
      },
    }),
  );
  await page.goto('/ais');
  await expect(page.getByRole('heading', { name: 'AI Library', exact: true })).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Patient Gardener' })).toBeVisible();
  const output = resolve(import.meta.dirname, '../../../../artifacts/ai-library/screenshots');
  mkdirSync(output, { recursive: true });
  for (const theme of ['light', 'dark']) {
    await page.evaluate(`document.documentElement.dataset.theme = '${theme}'`);
    await page.screenshot({
      path: resolve(output, `${testInfo.project.name}-${theme}-catalogue.png`),
      fullPage: true,
    });
    const result = await new AxeBuilder({ page })
      .withTags(['wcag2a', 'wcag2aa', 'wcag21aa'])
      .analyze();
    expect(result.violations).toEqual([]);
  }
  if (
    testInfo.project.name === 'desktop' &&
    !(await page.evaluate(() => matchMedia('(prefers-reduced-motion: reduce)').matches))
  ) {
    const firstCard = page.locator('.ai-card').first();
    await firstCard.hover();
    await expect(firstCard).toHaveCSS('transform', 'matrix(1, 0, 0, 1, 0, -3)');
    await page.screenshot({
      path: resolve(output, 'desktop-normal-motion-hover.png'),
      fullPage: true,
    });
  }
  // Returning from a release preserves the browsing task and restores keyboard focus.
  await page.getByRole('link', { name: 'Favourites', exact: true }).click();
  await page.getByRole('searchbox', { name: 'Search AIs' }).fill('patient');
  await expect(page).toHaveURL(/q=patient/);
  await page.getByRole('button', { name: 'Economy', exact: true }).click();
  await page.getByRole('combobox', { name: 'Sort AIs' }).selectOption('downloads');
  await page.getByRole('heading', { name: 'Patient Gardener' }).click();
  await expect(page.getByRole('link', { name: 'Download JavaScript (.js)' })).toBeVisible();
  await page.getByRole('combobox', { name: 'Version', exact: true }).selectOption('old-release');
  await expect(page.getByRole('link', { name: 'Download JavaScript (.js)' })).toHaveAttribute(
    'href',
    '/api/v1/ais/' + id + '/versions/old-release/file',
  );
  await page.getByRole('button', { name: 'Edit details', exact: true }).click();
  await expect(page.getByRole('textbox', { name: 'Name', exact: true })).toBeFocused();
  expect(
    (await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21aa']).analyze())
      .violations,
  ).toEqual([]);
  await page.getByRole('button', { name: 'Cancel editing' }).click();
  await expect(page.getByRole('button', { name: 'Edit details', exact: true })).toBeFocused();
  await page.getByRole('button', { name: 'Report', exact: true }).click();
  await expect(page.getByRole('combobox', { name: 'Reason', exact: true })).toBeFocused();
  expect(
    (await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21aa']).analyze())
      .violations,
  ).toEqual([]);
  await page.getByRole('button', { name: 'Cancel report' }).click();
  await expect(page.getByRole('button', { name: 'Report', exact: true })).toBeFocused();
  await page.getByRole('link', { name: '← Favourites', exact: true }).click();
  await expect(page.getByRole('searchbox', { name: 'Search AIs' })).toHaveValue('patient');
  await expect(page.getByRole('combobox', { name: 'Sort AIs' })).toHaveValue('downloads');
  await expect(page.getByRole('button', { name: 'Economy', exact: true })).toHaveAttribute(
    'aria-pressed',
    'true',
  );
  await expect(page.locator('#ai-card-' + id)).toBeFocused();
  await page.getByRole('heading', { name: 'Patient Gardener' }).click();
  await page.goBack();
  await expect(page.getByRole('searchbox', { name: 'Search AIs' })).toHaveValue('patient');
  await page.getByRole('heading', { name: 'Patient Gardener' }).click();
  await page.getByRole('combobox', { name: 'Version', exact: true }).selectOption('old-release');
  await page.getByRole('button', { name: '☆ Favourite' }).click();
  await expect(page.getByRole('button', { name: '★ Favourited' })).toBeVisible();
  await expect(page.getByRole('combobox', { name: 'Version', exact: true })).toHaveValue(
    'old-release',
  );
  await page.screenshot({
    path: resolve(output, `${testInfo.project.name}-detail.png`),
    fullPage: true,
  });
  expect(
    (await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21aa']).analyze())
      .violations,
  ).toEqual([]);
  await page.goto('/ais/new');
  const publish = page.getByRole('button', { name: 'Publish', exact: true });
  await expect(publish).toBeDisabled();
  await page.getByLabel('Bundled JavaScript file').setInputFiles({
    name: 'gardener.js',
    mimeType: 'text/javascript',
    buffer: Buffer.from('function step(){}'),
  });
  await expect(page.getByText('Ready to publish')).toBeVisible();
  await expect(publish).toBeEnabled();
  expect(
    (await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21aa']).analyze())
      .violations,
  ).toEqual([]);
  await page.screenshot({
    path: resolve(output, `${testInfo.project.name}-publish.png`),
    fullPage: true,
  });
  expect(await page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')).toBe(
    true,
  );
});
