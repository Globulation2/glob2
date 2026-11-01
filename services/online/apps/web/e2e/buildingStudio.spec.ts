import { expect, test } from '@playwright/test';
import { readBuildingArchive } from '@glob2/protocol/node';
import { AxeBuilder } from '@axe-core/playwright';
import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
test('authors a private family with revision-safe saving and portable export', async ({
  page,
  request,
  baseURL,
}, testInfo) => {
  if (!baseURL) throw new Error('The browser test needs its configured base URL.');
  const seed = (await (await request.get('/__seed')).json()) as { userSession: string };
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.goto('/building-studio');
  await page.getByRole('button', { name: 'Create a family' }).click();
  await expect(page.getByRole('heading', { name: 'Building family editor' })).toBeVisible();
  await page.getByLabel('Family name', { exact: true }).fill('Community kitchen');
  await page.getByLabel('hpMax', { exact: true }).fill('500');
  await page.getByText('Family experiments', { exact: true }).click();
  await page.getByRole('textbox', { name: 'Family experiments JSON', exact: true }).fill('[broken');
  await page.getByRole('button', { name: 'Save draft', exact: true }).click();
  await expect(
    page.getByRole('status').filter({ hasText: 'Resolve invalid fields before saving.' }),
  ).toBeVisible();
  await page.getByRole('button', { name: 'Review release settings' }).click();
  await expect(
    page.getByRole('button', { name: 'Publish saved family', exact: true, includeHidden: true }),
  ).toBeDisabled();
  await page.getByRole('button', { name: 'Close panel' }).click();
  page.once('dialog', (dialog) => dialog.accept());
  await page.reload();
  await page.getByRole('button', { name: 'Restore device copy' }).click();
  await page.getByText('Family experiments', { exact: true }).click();
  await expect(
    page.getByRole('textbox', { name: 'Family experiments JSON', exact: true }),
  ).toHaveValue('[broken');
  await page.getByRole('textbox', { name: 'Family experiments JSON', exact: true }).fill('[]');
  await page.getByRole('button', { name: 'Save draft', exact: true }).click();
  await expect(page.getByRole('status').filter({ hasText: 'Draft saved.' })).toBeVisible();
  await page.reload();
  await expect(page.getByLabel('Family name', { exact: true })).toHaveValue('Community kitchen');
  await expect(page.getByLabel('hpMax', { exact: true })).toHaveValue('500');
  await page.getByRole('button', { name: 'JSON', exact: true }).click();
  const raw = page.getByRole('textbox', { name: 'Complete family JSON', exact: true });
  const packageText = await raw.inputValue();
  const edited = packageText.replace('"hpMax": 500', '"hpMax": 600');
  expect(edited).not.toBe(packageText);
  await raw.fill(edited);
  await page.getByRole('button', { name: 'JSON', exact: true }).click();
  await expect(raw).toHaveValue(edited);
  await page.getByRole('button', { name: 'Save draft', exact: true }).click();
  await expect(page.getByRole('status').filter({ hasText: 'Draft saved.' })).toBeVisible();
  await page.getByRole('button', { name: 'Fields', exact: true }).click();
  await expect(page.getByLabel('hpMax', { exact: true })).toHaveValue('600');
  await page.getByRole('button', { name: 'Add stage', exact: true }).click();
  await expect(
    page.getByRole('combobox', { name: 'Stage', exact: true }).locator('option'),
  ).toHaveCount(2);
  await page.getByRole('button', { name: 'Save draft', exact: true }).click();
  await expect(page.getByRole('status').filter({ hasText: 'Draft saved.' })).toBeVisible();
  const download = page.waitForEvent('download');
  await page.getByRole('link', { name: 'Export saved package' }).click();
  const file = await download,
    stream = await file.createReadStream();
  const chunks: Buffer[] = [];
  for await (const chunk of stream) chunks.push(Buffer.from(chunk));
  expect(readBuildingArchive(Buffer.concat(chunks)).package.variants).toHaveLength(2);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(
    true,
  );
  const report = await new AxeBuilder({ page }).analyze();
  expect(report.violations).toEqual([]);
  if (process.env['SCREENSHOT_DIR']) {
    const dir = resolve(process.env['SCREENSHOT_DIR']);
    mkdirSync(dir, { recursive: true });
    await page.screenshot({
      path: resolve(dir, `building-studio-${testInfo.project.name}.png`),
      fullPage: true,
    });
  }
  page.once('dialog', (dialog) => dialog.dismiss());
  await page.getByRole('button', { name: 'Delete draft', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Building family editor' })).toBeVisible();
  page.once('dialog', (dialog) => dialog.accept());
  await page.getByRole('button', { name: 'Delete draft', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Building Studio', exact: true })).toBeVisible();
});

test('protects pending JSON on Back and restores it after Forward', async ({
  page,
  request,
  baseURL,
}) => {
  if (!baseURL) throw new Error('The browser test needs its configured base URL.');
  const seed = (await (await request.get('/__seed')).json()) as { userSession: string };
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.goto('/building-studio');
  await page.getByRole('button', { name: 'Create a family' }).click();
  await expect(page.getByRole('heading', { name: 'Building family editor' })).toBeVisible();
  const editorUrl = page.url();
  await page.getByText('Family experiments', { exact: true }).click();
  const pending = page.getByRole('textbox', { name: 'Family experiments JSON', exact: true });
  await pending.fill('[unfinished');
  page.once('dialog', (dialog) => dialog.dismiss());
  await page.goBack();
  await expect(page).toHaveURL(editorUrl);
  await expect(pending).toHaveValue('[unfinished');
  page.once('dialog', (dialog) => dialog.accept());
  await page.goBack();
  await expect(page.getByRole('heading', { name: 'Building Studio', exact: true })).toBeVisible();
  await page.goForward();
  await expect(page).toHaveURL(editorUrl);
  await page.getByRole('button', { name: 'Restore device copy' }).click();
  await page.getByText('Family experiments', { exact: true }).click();
  await expect(pending).toHaveValue('[unfinished');
  // Deleting a dirty draft needs its destructive confirmation, not a second leave dialog.
  page.once('dialog', (dialog) => dialog.accept());
  await page.getByRole('button', { name: 'Delete draft', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Building Studio', exact: true })).toBeVisible();
});
