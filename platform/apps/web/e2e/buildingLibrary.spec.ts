import { expect, test } from '@playwright/test';
import type { BuildingFamily, BuildingLibrary, SelfAccount } from '@glob2/protocol';
import { AxeBuilder } from '@axe-core/playwright';
import { readBuildingArchive } from '@glob2/protocol/node';
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
test('browses, downloads and forks a released family', async ({ page, request, baseURL }, info) => {
  if (!baseURL) throw new Error('The browser test needs its configured base URL.');
  const seed = (await (await request.get('/__seed')).json()) as { userSession: string };
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.goto('/buildings');
  await page.getByLabel('Search buildings').fill('Community kitchen');
  await page.getByRole('button', { name: 'Search', exact: true }).click();
  await page.getByRole('link', { name: 'Community kitchen', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Community kitchen', exact: true })).toBeVisible();
  await expect(page.getByRole('link', { name: 'Download family' })).toBeVisible();
  await expect(page.getByRole('textbox', { name: 'Family link', exact: true })).toHaveValue(
    page.url(),
  );
  await page.getByText('Use buildings in the game', { exact: true }).click();
  await expect(page.getByText(/For an unlisted family, copy its page link/)).toBeVisible();
  if (await page.getByRole('button', { name: 'Unlike', exact: true }).count())
    await page.getByRole('button', { name: 'Unlike', exact: true }).click();
  await page.getByRole('button', { name: 'Like', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Unlike', exact: true })).toBeVisible();
  if (await page.getByRole('button', { name: 'Remove favourite', exact: true }).count())
    await page.getByRole('button', { name: 'Remove favourite', exact: true }).click();
  await page.getByRole('button', { name: 'Favourite', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Remove favourite', exact: true })).toBeVisible();
  const pending = page.waitForEvent('download');
  await page.getByRole('link', { name: 'Download family' }).click();
  const stream = await (await pending).createReadStream(),
    chunks: Buffer[] = [];
  for await (const chunk of stream) chunks.push(Buffer.from(chunk));
  expect(readBuildingArchive(Buffer.concat(chunks)).package.variants).toHaveLength(1);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(
    true,
  );
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  if (process.env['SCREENSHOT_DIR']) {
    const dir = resolve(process.env['SCREENSHOT_DIR']);
    mkdirSync(dir, { recursive: true });
    await page.screenshot({
      path: resolve(dir, `building-library-${info.project.name}.png`),
      fullPage: true,
    });
  }
  await page.getByRole('button', { name: 'Fork into Studio' }).click();
  await expect(page.getByRole('heading', { name: 'Building family editor' })).toBeVisible();
  await expect(page.getByLabel('Family name', { exact: true })).toHaveValue(
    'Fork of Community kitchen',
  );
});

// Exercise owner controls independently of validator scheduling and seeded ownership.
// API integration tests cover real permissions and withdrawal persistence.
test('owner changes published visibility and confirms withdrawal', async ({
  page,
  request,
  baseURL,
}) => {
  if (!baseURL) throw new Error('The browser test needs its configured base URL.');
  const seed = (await (await request.get('/__seed')).json()) as { userSession: string };
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  const me = (await (await page.request.get('/api/v1/accounts/me')).json()) as SelfAccount;
  const list = (await (await page.request.get('/api/v1/buildings')).json()) as BuildingLibrary;
  const source = list.items.find((family) => family.name === 'Community kitchen');
  if (!source) throw new Error('Missing library browser fixture.');
  let family: BuildingFamily = { ...source, owner: { id: me.id, displayName: me.displayName } };
  let withdrawals = 0;
  await page.route(`**/api/v1/buildings/${family.id}`, async (route) => {
    if (route.request().method() === 'PATCH') {
      const changes = route.request().postDataJSON() as Pick<
        BuildingFamily,
        'name' | 'description' | 'visibility'
      >;
      family = { ...family, ...changes, updatedAt: new Date().toISOString() };
    }
    if (route.request().method() === 'DELETE') {
      withdrawals++;
      await route.fulfill({ status: 204 });
      return;
    }
    await route.fulfill({ json: family });
  });
  await page.goto('/buildings/' + family.id);
  await page.getByText('Manage published family', { exact: true }).click();
  await page
    .getByRole('combobox', { name: 'Published visibility', exact: true })
    .selectOption('private');
  await page
    .getByRole('textbox', { name: 'Published description', exact: true })
    .fill('Only for my own maps.');
  await page.getByRole('button', { name: 'Save published details' }).click();
  await expect(page.getByRole('status').filter({ hasText: 'Saved.' })).toBeVisible();
  expect(family.visibility).toBe('private');
  expect(family.description).toBe('Only for my own maps.');
  await expect(page.locator('p').filter({ hasText: /^Only for my own maps\.$/ })).toBeVisible();
  page.once('dialog', (dialog) => dialog.dismiss());
  await page.getByRole('button', { name: 'Withdraw published family' }).click();
  expect(withdrawals).toBe(0);
  page.once('dialog', (dialog) => dialog.accept());
  await page.getByRole('button', { name: 'Withdraw published family' }).click();
  await expect(page.getByRole('heading', { name: 'Building library', exact: true })).toBeVisible();
  expect(withdrawals).toBe(1);
});
