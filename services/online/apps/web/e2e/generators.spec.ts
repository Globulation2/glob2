import { mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { expect, test } from '@playwright/test';
import { AxeBuilder } from '@axe-core/playwright';
import { generator, id, report, version } from '../test/generatorFixture.ts';
test('generator catalogue, exact release evidence and staged publication', async ({
  page,
}, info) => {
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
  await page.route('**/api/v1/generators**', (r) => {
    const path = new URL(r.request().url()).pathname;
    if (path.endsWith('preview.png'))
      return r.fulfill({
        contentType: 'image/png',
        body: readFileSync(resolve(import.meta.dirname, 'fixtures/maps/isles.png')),
      });
    return r.fulfill({
      json:
        path === '/api/v1/generators'
          ? { items: [generator] }
          : { generator, versions: [version], viewer: { owner: true, moderator: false } },
    });
  });
  await page.route('**/api/v1/generator-uploads**', (r) =>
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
  const output = resolve(
    import.meta.dirname,
    '../../../../artifacts/generator-library/screenshots',
  );
  mkdirSync(output, { recursive: true });
  await page.goto('/generators');
  await expect(page.getByRole('heading', { name: 'River Country' })).toBeVisible();
  for (const theme of ['light', 'dark']) {
    await page.evaluate((t) => {
      document.documentElement.dataset.theme = t;
    }, theme);
    await page.screenshot({
      path: resolve(output, `${info.project.name}-${theme}-catalogue.png`),
      fullPage: true,
    });
    expect(
      (await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21aa']).analyze())
        .violations,
    ).toEqual([]);
  }
  await page.getByRole('link', { name: 'Example of River Country River Country' }).click();
  await page.getByText('Controls and compatibility evidence').click();
  await expect(page.getByText(/no suitable colony sites/)).toBeVisible();
  await page.screenshot({
    path: resolve(output, `${info.project.name}-release.png`),
    fullPage: true,
  });
  await page.goto('/generators/new');
  await expect(page.getByRole('button', { name: 'Publish release' })).toBeDisabled();
  await page.getByLabel('Package', { exact: true }).setInputFiles({
    name: 'generator.json',
    mimeType: 'application/json',
    buffer: Buffer.from('{}'),
  });
  await page.getByRole('button', { name: 'Validate package' }).click();
  await expect(page.getByRole('heading', { name: 'Technical checks passed' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Publish release' })).toBeEnabled();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(
    true,
  );
  await page.screenshot({
    path: resolve(output, `${info.project.name}-publication.png`),
    fullPage: true,
  });
  expect(
    (await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21aa']).analyze())
      .violations,
  ).toEqual([]);
});
