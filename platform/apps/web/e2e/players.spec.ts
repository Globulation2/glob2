import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { test, expect } from '@playwright/test';
import { AxeBuilder } from '@axe-core/playwright';
import sharp from 'sharp';
import type { SeededHistory } from '../../api/test/historySeed.ts';
let seed: SeededHistory;
test.beforeAll(async ({ request }) => {
  seed = (await (await request.get('/__seed')).json()) as SeededHistory;
});
test.use({ reducedMotion: 'reduce' });
for (const theme of ['light', 'dark'] as const) {
  test(`directory, AI profile, rankings and photos (${theme})`, async ({ page }, info) => {
    await page.emulateMedia({ colorScheme: theme });
    await page.goto('/players');
    const search = page.getByRole('combobox', { name: 'Find a player' });
    await expect(page.getByRole('option').first()).toBeVisible();
    await search.fill('nico');
    await expect(page.getByRole('option')).toHaveCount(1);
    const violations = (
      await new AxeBuilder({ page })
        .withTags(['wcag2a', 'wcag2aa', 'wcag21aa', 'wcag22aa'])
        .analyze()
    ).violations;
    expect(violations).toEqual([]);
    const dir = resolve(import.meta.dirname, '../../../../artifacts/web-app/players');
    mkdirSync(dir, { recursive: true });
    await page.screenshot({
      path: resolve(dir, `${info.project.name}-${theme}-directory.png`),
      fullPage: true,
    });
    await search.press('Escape');
    await expect(search).toHaveValue('nico');
    await expect(search).toHaveAttribute('aria-expanded', 'false');
    await search.press('ArrowUp');
    await expect(page.getByRole('option')).toHaveAttribute('aria-selected', 'true');
    await search.press('Enter');
    await expect(page.getByRole('heading', { level: 1 })).toContainText('Nicowar');
    await expect(page.getByTestId('match-row').first()).toBeVisible();
    expect(
      (
        await new AxeBuilder({ page })
          .withTags(['wcag2a', 'wcag2aa', 'wcag21aa', 'wcag22aa'])
          .analyze()
      ).violations,
    ).toEqual([]);
    await page.screenshot({
      path: resolve(dir, `${info.project.name}-${theme}-ai-profile.png`),
      fullPage: true,
    });
    await page.goto('/leaderboard');
    await page.getByRole('button', { name: 'AI', exact: true }).click();
    await expect(page.getByTestId('leaderboard-row').first()).toContainText('AI');
    await page
      .context()
      .addCookies([
        { name: 'glob2_session', value: seed.userSession, url: info.project.use.baseURL as string },
      ]);
    await page.goto('/account');
    // Distinct quadrants reveal EXIF orientation and crop mistakes that a flat color hides.
    const photo = await sharp(
      Buffer.from(
        `<svg width="640" height="400"><rect width="320" height="200" fill="red"/><rect x="320" width="320" height="200" fill="lime"/><rect y="200" width="320" height="200" fill="blue"/><rect x="320" y="200" width="320" height="200" fill="yellow"/></svg>`,
      ),
    )
      .jpeg()
      .withMetadata({ orientation: 6 })
      .toBuffer();
    const upload = () =>
      page
        .getByLabel('Choose profile photo')
        .setInputFiles({ name: 'portrait.jpg', mimeType: 'image/jpeg', buffer: photo });
    let uploadRequests = 0;
    page.on('request', (request) => {
      if (request.method() === 'PUT' && request.url().endsWith('/api/v1/accounts/me/avatar'))
        uploadRequests++;
    });
    await upload();
    await expect(page.getByRole('dialog')).toBeVisible();
    await page.getByRole('button', { name: 'Cancel', exact: true }).click();
    expect(uploadRequests).toBe(0);
    await expect(page.getByRole('dialog')).toHaveCount(0);
    await expect(page.getByRole('button', { name: 'Upload photo' })).toBeFocused();
    await upload();
    await expect(page.getByRole('dialog')).toBeVisible();
    await expect(page.getByRole('button', { name: 'Save photo' })).toBeEnabled();
    await page.getByRole('slider', { name: 'Zoom' }).fill('1.5');
    const crop = page.getByRole('group', { name: 'Photo position' });
    const preview = page.locator('.reactEasyCrop_Image');
    const originalTransform = await preview.getAttribute('style');
    await crop.focus();
    await crop.press('ArrowRight');
    await expect(preview).not.toHaveAttribute('style', originalTransform ?? '');
    expect(
      (
        await new AxeBuilder({ page })
          .withTags(['wcag2a', 'wcag2aa', 'wcag21aa', 'wcag22aa'])
          .analyze()
      ).violations,
    ).toEqual([]);
    await page.screenshot({
      path: resolve(dir, `${info.project.name}-${theme}-crop.png`),
      fullPage: true,
    });
    await page.route(
      '**/api/v1/accounts/me/avatar',
      (route) =>
        route.fulfill({
          status: 503,
          json: { code: 'unavailable', message: 'Photo service temporarily unavailable.' },
        }),
      { times: 1 },
    );
    await page.getByRole('button', { name: 'Save photo' }).click();
    await expect(page.getByRole('alert')).toContainText('Photo service temporarily unavailable.');
    await expect(page.getByRole('dialog')).toBeVisible();
    await expect(page.getByRole('button', { name: 'Cancel', exact: true })).toBeEnabled();
    await page.getByRole('button', { name: 'Save photo' }).click();
    await expect(page.getByText('Your uploaded photo')).toBeVisible();
    const avatar = page.locator('.profile-photo-preview img');
    await expect(avatar).toBeVisible();
    const avatarSrc = await avatar.getAttribute('src');
    expect(avatarSrc).toBeTruthy();
    const response = await page.request.get(avatarSrc ?? '');
    const pixels = await sharp(await response.body())
      .removeAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true });
    expect(pixels.info.width).toBe(512);
    expect(pixels.info.height).toBe(512);
    const sample = (x: number, y: number) => [
      ...pixels.data.subarray((y * 512 + x) * 3, (y * 512 + x) * 3 + 3),
    ];
    // Orientation 6 rotates clockwise: blue moves to top-left and red to top-right.
    expect(sample(128, 128)[2]).toBeGreaterThan(230);
    expect(sample(384, 128)[0]).toBeGreaterThan(230);
    expect(sample(384, 128)[1]).toBeLessThan(25);
    await page.getByRole('button', { name: 'Use initials' }).click();
    await expect(page.getByText('Initials only')).toBeVisible();
    expect(await page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')).toBe(
      true,
    );
  });
}

test('keyboard directory navigation keeps the active result in view', async ({ page }) => {
  await page.goto('/players');
  const search = page.getByRole('combobox', { name: 'Find a player' });
  await expect(page.getByRole('option').first()).toBeVisible();
  await search.focus();
  await search.press('ArrowUp');
  const active = page.locator('[role="option"][aria-selected="true"]');
  await expect(active).toBeInViewport();
  await expect(search).toHaveAttribute(
    'aria-activedescendant',
    (await active.getAttribute('id')) ?? '',
  );
});
