import { AxeBuilder } from '@axe-core/playwright';
import { test, expect } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, mkdirSync, copyFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import type { MusicMetadata, MusicRelease, MusicTrack } from '@glob2/protocol';
let directory: string, release: MusicRelease;
test.beforeAll(() => {
  directory = process.env['MUSIC_FIXTURE_DIR'] ?? mkdtempSync(join(tmpdir(), 'glob2-music-web-'));
  if (!process.env['MUSIC_FIXTURE_DIR'])
    execFileSync('python3', [
      resolve(import.meta.dirname, '../../../../tools/music/tests/make_community_fixture.py'),
      directory,
    ]);
  const metadata = JSON.parse(
    readFileSync(join(directory, 'metadata.json'), 'utf8'),
  ) as MusicMetadata & { id: string };
  const result = JSON.parse(readFileSync(join(directory, 'result.json'), 'utf8')) as {
    frames: number;
    tracks: MusicTrack[];
    warnings: string[];
  };
  release = {
    id: metadata.id,
    ownerId: '00000000-0000-4000-8000-000000000001',
    metadata,
    status: 'published',
    createdAt: '2026-01-01T00:00:00.000Z',
    frames: result.frames,
    tracks: result.tracks.map((t) => ({
      ...t,
      url: `/api/v1/music/${metadata.id}/tracks/${t.mood}`,
    })),
    warnings: result.warnings,
    inspection: null,
    uploaded: [],
    coverUrl: null,
    likes: 12,
    downloads: 4,
    liked: false,
    hidden: false,
    error: null,
  };
});
test.afterAll(() => {
  if (directory && !process.env['MUSIC_FIXTURE_DIR'])
    rmSync(directory, { recursive: true, force: true });
});
test('catalogue, bounded WASM playback, fades and bulk selection', async ({ page }, testInfo) => {
  await page.route('**/api/v1/music**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    const mood = ['calm', 'building', 'combat'].findIndex((m) => path.endsWith('/tracks/' + m));
    if (mood >= 0)
      return route.fulfill({
        contentType: 'audio/ogg',
        body: readFileSync(join(directory, `a${mood + 1}.opus`)),
      });
    if (path === '/api/v1/music') return route.fulfill({ json: { items: [release], next: null } });
    return route.fulfill({ json: release });
  });
  await page.addInitScript(`{
    const OriginalWorker=window.Worker;
    window.Worker=class extends OriginalWorker {
      constructor(...args) { super(...args); this.addEventListener('message',event=>{if(event.data.decoderHeapBytes) window.__musicHeapBytes=event.data.decoderHeapBytes;}); }
    };
  }`);
  await page.goto('/music');
  await expect(page.getByRole('heading', { name: 'Music for your colony' })).toBeVisible();
  await page.getByRole('button', { name: 'Preview', exact: true }).click();
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Pause', exact: true })).toBeVisible({
    timeout: 20_000,
  });
  await expect
    .poll(async () =>
      Number(await page.getByRole('slider', { name: 'Playback position' }).inputValue()),
    )
    .toBeGreaterThan(1);
  const heap = await page.evaluate('window.__musicHeapBytes');
  if (heap) {
    expect(heap).toBeLessThanOrEqual(128 * 1024 * 1024);
    console.log(JSON.stringify({ seconds: release.frames / 48000, decoderHeapBytes: heap }));
  }
  await page.getByRole('button', { name: 'Crossfade to Combat' }).click();
  await expect(page.getByRole('button', { name: 'Crossfade to Combat' })).toContainText('100%');
  await page.getByText('Test crossfades', { exact: true }).click();
  await page.getByRole('slider', { name: /Manual blend/ }).fill('0.5');
  await expect(page.getByRole('button', { name: 'Crossfade to Calm' })).toContainText('50%');
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  for (const theme of ['light', 'dark']) {
    await page.evaluate('document.documentElement.dataset.theme = ' + JSON.stringify(theme));
    const result = await new AxeBuilder({ page })
      .include('.music-preview-panel')
      .withTags(['wcag2a', 'wcag2aa', 'wcag21aa'])
      .analyze();
    expect(result.violations).toEqual([]);
    await page.locator('.music-preview-panel').evaluate((panel) => {
      panel.scrollTop = 0;
    });
    await page.screenshot({
      path: testInfo.outputPath(`music-player-${theme}.png`),
      fullPage: false,
    });
  }
  expect(await page.evaluate('document.documentElement.scrollWidth <= innerWidth')).toBe(true);
  await page.keyboard.press('Escape');
  await expect(page.getByRole('dialog')).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Preview', exact: true })).toBeFocused();
  await page.getByRole('checkbox', { name: 'Select for download' }).check();
  await expect(page.getByRole('button', { name: 'Download selected' })).toBeVisible();
  await page.screenshot({ path: testInfo.outputPath('music-catalogue.png'), fullPage: true });
});

test('upload → worker conversion → audition → publish → like → bulk ZIP', async ({
  page,
}, testInfo) => {
  test.skip(
    testInfo.project.name !== 'desktop' || process.env['MUSIC_E2E_PROCESSING'] !== '1',
    'Opt-in real worker integration on desktop',
  );
  const seed = (await (await page.request.get('/__seed')).json()) as { userSession: string };
  await page.context().addCookies([
    {
      name: 'glob2_session',
      value: seed.userSession,
      url: testInfo.project.use.baseURL ?? `http://127.0.0.1:${process.env['PORT'] ?? 4280}`,
    },
  ]);
  await page.goto('/music/new');
  await page.getByLabel('Title', { exact: true }).fill('Moss lantern');
  await page.getByLabel('Artist', { exact: true }).fill('Browser test composer');
  await page.getByLabel('Description', { exact: true }).fill('End-to-end community music release');
  await page.getByLabel('Credits', { exact: true }).fill('Synthetic test fixture');
  await page.getByLabel(/I can share/).check();
  await page.getByRole('button', { name: 'Continue to uploads' }).click();
  await expect(page.getByRole('heading', { name: 'Prepare this release' })).toBeVisible();
  for (const [i, mood] of ['Calm', 'Building', 'Combat'].entries()) {
    await page.getByLabel(mood, { exact: true }).setInputFiles(join(directory, `a${i + 1}.opus`));
    await expect(page.getByText(`${mood} ✓ uploaded`, { exact: true })).toBeVisible();
  }
  await page.getByRole('button', { name: 'Inspect tracks', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Convert and prepare preview' })).toBeVisible({
    timeout: 45_000,
  });
  await page.getByRole('button', { name: 'Convert and prepare preview' }).click();
  await expect(page.getByRole('button', { name: 'Publish this release' })).toBeVisible({
    timeout: 45_000,
  });
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Pause', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await page.getByRole('button', { name: 'Publish this release' }).click();
  await page.getByRole('button', { name: /♥ 0 · Like/ }).click();
  await expect(page.getByRole('button', { name: /♥ 1 · Unlike/ })).toBeVisible();
  await page.getByRole('link', { name: '← Music library' }).click();
  await page.getByLabel('Search', { exact: true }).fill('Moss lantern');
  await page.getByRole('checkbox', { name: 'Select for download' }).check();
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Download selected' }).click();
  const file = await download;
  await file.saveAs(testInfo.outputPath('downloaded-music.zip'));
  const output = resolve(import.meta.dirname, '../../../../artifacts/music');
  mkdirSync(output, { recursive: true });
  copyFileSync(testInfo.outputPath('downloaded-music.zip'), join(output, 'web-release.zip'));
});
