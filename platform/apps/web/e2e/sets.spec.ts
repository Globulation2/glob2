import { deflateSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { expect, test } from '@playwright/test';
import type { SeededHistory } from '../../api/test/historySeed.ts';

test('custom definitions and PNG mappings survive save and reload', async ({
  page,
  request,
  baseURL,
}) => {
  if (!baseURL) throw Error('Test server URL is required.');
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.goto('/sets/new');
  await expect(
    page.getByRole('link', { name: 'Terrain & resources', exact: true }).first(),
  ).toHaveAttribute('href', '/sets');
  await page
    .getByLabel('Title', { exact: true })
    .fill('Browser terrain ' + test.info().project.name);
  await page.getByRole('button', { name: 'Add terrain', exact: true }).click();
  await page.getByLabel('Name', { exact: true }).fill('Library moss');
  const chunk = (name: string, bytes: Buffer) => {
    const body = Buffer.concat([Buffer.from(name), bytes]);
    let crc = 0xffffffff;
    for (const byte of body) {
      crc ^= byte;
      for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
    const size = Buffer.alloc(4),
      checksum = Buffer.alloc(4);
    size.writeUInt32BE(bytes.length);
    checksum.writeUInt32BE((crc ^ 0xffffffff) >>> 0);
    return Buffer.concat([size, body, checksum]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(32);
  header.writeUInt32BE(32, 4);
  header[8] = 8;
  header[9] = 6;
  const pixels = Buffer.alloc(32 * 129, 0);
  for (let y = 0; y < 32; y++)
    for (let x = 0; x < 32; x++) {
      const offset = y * 129 + 1 + x * 4;
      pixels[offset] = 80;
      pixels[offset + 1] = 140;
      pixels[offset + 2] = 90;
      pixels[offset + 3] = 255;
    }
  const png = Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(pixels)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
  const hash = createHash('sha256').update(png).digest('hex');
  await page
    .getByLabel('Upload PNG', { exact: true })
    .setInputFiles({ name: 'moss.png', mimeType: 'image/png', buffer: png });
  await expect(page.getByRole('combobox', { name: 'Inspect sheet', exact: true })).toHaveValue(
    hash,
    { timeout: 30000 },
  );
  // Repair a mistaken grid without abandoning the draft or duplicating image bytes.
  await page.getByLabel('Frame width', { exact: true }).fill('16');
  await page.getByLabel('Frame height', { exact: true }).fill('16');
  await page
    .getByLabel('Upload PNG', { exact: true })
    .setInputFiles({ name: 'moss.png', mimeType: 'image/png', buffer: png });
  const sheetOption = page
    .getByRole('combobox', { name: 'Spritesheet', exact: true })
    .locator(`option[value="data/sets/${hash}"]`);
  await expect(sheetOption).toContainText('16×16');
  await page.getByLabel('Frame width', { exact: true }).fill('32');
  await page.getByLabel('Frame height', { exact: true }).fill('32');
  await page
    .getByLabel('Upload PNG', { exact: true })
    .setInputFiles({ name: 'moss.png', mimeType: 'image/png', buffer: png });
  await expect(sheetOption).toContainText('32×32');
  await page.getByRole('button', { name: 'Remove sheet', exact: true }).click();
  await expect(page.getByRole('combobox', { name: 'Inspect sheet', exact: true })).toHaveValue('');
  await page
    .getByLabel('Upload PNG', { exact: true })
    .setInputFiles({ name: 'moss.png', mimeType: 'image/png', buffer: png });
  await expect(page.getByRole('combobox', { name: 'Inspect sheet', exact: true })).toHaveValue(
    hash,
  );
  await page
    .getByRole('combobox', { name: 'Spritesheet', exact: true })
    .selectOption('data/sets/' + hash);

  await page.getByRole('button', { name: 'Save draft', exact: true }).click();
  await expect(page).toHaveURL(/\/sets\/drafts\//);
  await expect(page.getByText('Draft saved', { exact: false })).toBeVisible();
  await page.reload();
  await expect(page.getByLabel('Name', { exact: true })).toHaveValue('Library moss');
  await expect(page.getByRole('combobox', { name: 'Spritesheet', exact: true })).toHaveValue(
    'data/sets/' + hash,
  );
  await page.getByRole('button', { name: 'Review & publish', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Publish this release' })).toBeDisabled();
  await page.getByRole('dialog').getByRole('button', { name: 'Close panel', exact: true }).click();
  if (process.env['SET_E2E_ENGINE'] === '1') {
    test.setTimeout(180000);
    await page.getByRole('button', { name: 'Resources', exact: true }).click();
    await page.getByRole('button', { name: 'Add resource', exact: true }).click();
    await page
      .getByRole('combobox', { name: 'Spritesheet', exact: true })
      .selectOption('data/sets/' + hash);
    const resourceFrames = page.getByLabel('Frame', { exact: true });
    for (let i = 0; i < (await resourceFrames.count()); i++) await resourceFrames.nth(i).fill('0');
    await page.getByLabel('Animation Frames', { exact: true }).fill('1');
    await page.getByRole('button', { name: 'Preview current changes', exact: true }).click();
    await expect(
      page.getByText('Rendered by the browser game engine.', { exact: false }),
    ).toBeVisible({ timeout: 120000 });
    const preview = page.getByRole('img', {
      name: 'Current custom terrain and resource sprites rendered by the game',
    });
    await expect(preview).toBeVisible();
    expect(await preview.evaluate((img) => (img as HTMLImageElement).naturalWidth)).toBeGreaterThan(
      0,
    );
    expect(
      await preview.evaluate(async (node) => {
        const img = node as HTMLImageElement;
        await img.decode();
        const canvas = document.createElement('canvas');
        canvas.width = img.naturalWidth;
        canvas.height = img.naturalHeight;
        const context = canvas.getContext('2d');
        if (!context) throw Error('Cannot inspect preview pixels.');
        context.drawImage(img, 0, 0);
        return [...context.getImageData(32, 32, 1, 1).data];
      }),
    ).toEqual([80, 140, 90, 255]);
    expect(
      await preview.evaluate((node) => {
        const img = node as HTMLImageElement;
        const canvas = document.createElement('canvas');
        canvas.width = img.naturalWidth;
        canvas.height = img.naturalHeight;
        const context = canvas.getContext('2d');
        if (!context) throw Error('Cannot inspect preview pixels.');
        context.drawImage(img, 0, 0);
        return [...context.getImageData(96, 32, 1, 1).data];
      }),
    ).toEqual([80, 140, 90, 255]);
    await page.getByRole('button', { name: 'Terrain', exact: true }).click();
    await preview.screenshot({
      path: '../../../artifacts/sets/' + test.info().project.name + '-engine-preview.png',
    });
  }
  await page.getByLabel('Name', { exact: true }).fill('Edited moss');
  await expect(page.getByText('Unsaved changes', { exact: false })).toBeVisible();
  await page.getByRole('button', { name: 'Save draft', exact: true }).click();
  await expect(page.getByText('Draft saved', { exact: false })).toBeVisible();
  expect(
    await page.evaluate(
      'document.documentElement.scrollWidth - document.documentElement.clientWidth',
    ),
  ).toBeLessThanOrEqual(0);
  await page.screenshot({
    path: '../../../artifacts/sets/' + test.info().project.name + '-workspace.png',
    fullPage: true,
  });
  await page.setViewportSize({ width: 320, height: 860 });
  expect(
    await page.evaluate(
      'document.documentElement.scrollWidth - document.documentElement.clientWidth',
    ),
  ).toBeLessThanOrEqual(0);
  await page.screenshot({
    path: '../../../artifacts/sets/' + test.info().project.name + '-workspace-320.png',
    fullPage: true,
  });
});

test('shared map credits remain available on narrow screens', async ({ page, request }) => {
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page.setViewportSize({ width: 320, height: 860 });
  await page.goto('/maps/' + seed.mapId);
  const credits = page.getByRole('region', { name: 'Custom content credits' });
  await expect(credits).toBeVisible();
  await credits.locator('summary').scrollIntoViewIfNeeded();
  await credits.locator('summary').click();
  await expect(credits.getByText('Moss theme · CC-BY-4.0', { exact: true })).toBeVisible();
  await expect(credits.getByText('Fixture artist · CC-BY-4.0', { exact: false })).toBeVisible();
  await page.screenshot({
    path: '../../../artifacts/sets/' + test.info().project.name + '-map-credits.png',
    fullPage: true,
  });
  expect(
    await page.evaluate(
      'document.documentElement.scrollWidth - document.documentElement.clientWidth',
    ),
  ).toBeLessThanOrEqual(0);
});

test('cancelling browser Back retains unsaved set changes', async ({ page, request, baseURL }) => {
  if (!baseURL) throw Error('Test server URL is required.');
  const seed = (await (await request.get('/__seed')).json()) as SeededHistory;
  await page
    .context()
    .addCookies([{ name: 'glob2_session', value: seed.userSession, url: baseURL }]);
  await page.goto('/sets');
  await page.getByRole('link', { name: 'Create a set', exact: true }).click();
  await page.getByLabel('Title', { exact: true }).fill('Keep this unsaved set');
  const dismissed = new Promise<void>((resolve) =>
    page.once('dialog', async (dialog) => {
      expect(dialog.message()).toContain('discard unsaved changes');
      await dialog.dismiss();
      resolve();
    }),
  );
  await page.evaluate(() => history.back());
  await dismissed;
  await expect(page).toHaveURL(/\/sets\/new$/);
  await expect(page.getByLabel('Title', { exact: true })).toHaveValue('Keep this unsaved set');
  const accepted = new Promise<void>((resolve) =>
    page.once('dialog', async (dialog) => {
      await dialog.accept();
      resolve();
    }),
  );
  await page.evaluate(() => history.back());
  await accepted;
  await expect(page).toHaveURL(/\/sets$/);
});
