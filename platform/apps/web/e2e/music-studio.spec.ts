import { readFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { test, expect } from '@playwright/test';
import { AxeBuilder } from '@axe-core/playwright';
import type { MusicTrack } from '@glob2/protocol';
import type { SeededHistory } from '../../api/test/historySeed.ts';
test('music workspace exposes revision checks and keeps publication explicit', async ({
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
  const id = '11111111-1111-4111-8111-111111111111',
    version = '22222222-2222-4222-8222-222222222222';
  const second = '33333333-3333-4333-8333-333333333333';
  const writes: string[] = [];
  let holdTracks = false;
  let releaseTracks: (() => void) | undefined;
  const audioGate = new Promise<void>((resolve) => {
    releaseTracks = resolve;
  });
  const fixtureDirectory = process.env['MUSIC_FIXTURE_DIR'];
  const realAudio = fixtureDirectory
    ? (JSON.parse(readFileSync(join(fixtureDirectory, 'result.json'), 'utf8')) as {
        frames: number;
        tracks: MusicTrack[];
      })
    : undefined;
  if (fixtureDirectory)
    await page.context().route('**/api/v1/music/studio-test/tracks/*', async (route) => {
      if (holdTracks) await audioGate;
      const mood = ['calm', 'building', 'combat'].indexOf(
        new URL(route.request().url()).pathname.split('/').at(-1) ?? '',
      );
      return route.fulfill({
        contentType: 'audio/ogg',
        body: readFileSync(join(fixtureDirectory, `a${mood + 1}.opus`)),
      });
    });
  await page.route('**/api/v1/music-studio/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (route.request().method() === 'POST') {
      writes.push(path);
      return route.fulfill({ json: { ok: true } });
    }
    if (path.endsWith('/events'))
      return route.fulfill({ contentType: 'text/event-stream', body: ': connected\n\n' });
    if (path.endsWith('/account'))
      return route.fulfill({
        json: { enabled: true, available: 2, reserved: 0, packs: [], usage: [] },
      });
    if (path.endsWith('/threads'))
      return route.fulfill({ json: { items: [{ id, title: 'Moss & morning light' }] } });
    if (path.endsWith('/progress'))
      return route.fulfill({
        json: {
          requestId: path.split('/requests/')[1]?.split('/')[0],
          historical: false,
          notes: [
            {
              text: 'The flute melody now breathes between phrases. Combat introduces a soft low-string figure.',
              attempt: 2,
            },
          ],
          artifacts: [],
          stages: [
            ['prepare', 'Compose the music'],
            ['score', 'Check the score'],
            ['render', 'Render the instruments'],
            ['master', 'Mix and master'],
            ['checks', 'Validate the audio'],
            ['ready', 'Ready to listen'],
          ].map(([id, label]) => ({ id, label, status: 'complete' })),
          checks: [
            'format',
            'loudness',
            'seam',
            'repetition',
            'alignment',
            'contrast',
            'noise',
            'balance',
            'audibility',
            'dropout',
          ].map((label) => ({
            id: label,
            label,
            attempt: 2,
            status: label === 'balance' ? 'warn' : 'pass',
            measures: [
              {
                name: `${label}.calm`,
                status: label === 'balance' ? 'warn' : 'pass',
                value: label === 'balance' ? 0.38 : 0,
                threshold: 'Within the soundtrack target',
                detail:
                  label === 'balance'
                    ? 'A little warmth in the low mids. Listen under game effects.'
                    : 'Measured on the final encoded audio.',
                unit: '',
              },
            ],
          })),
        },
      });
    return route.fulfill({
      json: {
        id,
        title: 'Moss & morning light',
        cursor: '0',
        messages: [
          {
            id: 'm1',
            role: 'user',
            text: 'A cozy woodland theme with flute and harp. Calm should feel spacious; combat should stay warm.',
            created_at: '2026-10-05T00:00:00Z',
          },
          {
            id: 'm2',
            role: 'assistant',
            text: 'I’ll build a wandering flute melody, answering harp phrases and a gentle return. All three moods share the same harmonic journey.',
            created_at: '2026-10-05T00:00:01Z',
          },
        ],
        requests: [version, second].map((revision, i) => ({
          id: revision,
          thread_id: id,
          kind: 'generate',
          status: 'ready',
          input: {
            settings: { pipeline: 'acoustic-v1', seed: 4 },
            brief: '',
            messages: [],
            pipelineVersion: 'music-v1',
          },
          release_id: `release-${i}`,
          charged: true,
          error: null,
          created_at: `2026-10-05T00:00:0${i + 2}Z`,
        })),
      },
    });
  });
  await page.route('**/api/v1/music/release-*', (route) =>
    route.fulfill({
      json: {
        id: new URL(route.request().url()).pathname.split('/').at(-1),
        timelineId: 'a'.repeat(64),
        status: 'ready',
        metadata: {
          title: 'Moss & morning light',
          description: 'Woodwinds, harp and a warm pulse for a growing colony.',
          license: 'CC-BY-4.0',
        },
        frames: realAudio?.frames ?? 3840000,
        tracks: realAudio
          ? realAudio.tracks.map((track) => ({
              ...track,
              url: `/api/v1/music/studio-test/tracks/${track.mood}`,
            }))
          : ['calm', 'building', 'combat'].map((m, i) => ({
              mood: m,
              url: '/unused',
              waveform: Array.from(
                { length: 160 },
                (_, x) => Math.abs(Math.sin(x * 0.2 + i) * Math.cos(x * 0.061)) * 0.6,
              ),
            })),
      },
    }),
  );
  if (info.project.name === 'desktop') await page.setViewportSize({ width: 1470, height: 730 });
  const narrow = (page.viewportSize()?.width ?? 1280) < 850;
  await page.goto(`/music-studio/${id}`);
  await expect(page.getByRole('heading', { name: 'AI Music Studio' })).toBeVisible();
  if (narrow)
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  await expect(page.getByRole('button', { name: 'Play', exact: true })).toBeInViewport();
  const seek = page.getByRole('slider', { name: 'Playback position' });
  await seek.scrollIntoViewIfNeeded();
  await expect(seek).toBeInViewport();
  const validation = page.getByRole('heading', { name: 'Audio quality checks' });
  await validation.scrollIntoViewIfNeeded();
  await expect(validation).toBeInViewport();
  // Scrolling a hidden-overflow ancestor can reveal content in automation even
  // though users cannot reach it. The workspace must contain its full content.
  expect(
    await page.locator('.music-studio').evaluate((root) => {
      return root.scrollHeight <= root.clientHeight + 1;
    }),
  ).toBe(true);
  await expect(page.getByRole('heading', { name: 'Audio quality checks' })).toBeVisible();
  await page.getByText('Show technical results', { exact: false }).click();
  await page.locator('.music-technical').getByText('Mix balance', { exact: true }).click();
  await expect(
    page
      .locator('.music-technical')
      .getByText('A little warmth in the low mids. Listen under game effects.'),
  ).toBeVisible();
  await page.getByRole('button', { name: 'V1 Ready' }).click();
  await expect(page.getByRole('heading', { name: 'Version 1', exact: true })).toBeVisible();
  await page.getByRole('combobox', { name: 'Compare revision' }).selectOption(second);
  await expect(page.getByRole('heading', { name: 'Version 2 · comparing' })).toBeVisible();
  await expect(page.locator('.music-player')).toHaveCount(1);
  await page.screenshot({ path: info.outputPath('studio-comparison.png'), fullPage: true });
  if (realAudio) {
    await page.getByRole('button', { name: 'Crossfade to Combat' }).click();
    await page.getByRole('slider', { name: 'Volume' }).fill('0.4');
    holdTracks = true;
    await page.getByRole('button', { name: 'Play', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Loading music…' })).toBeVisible();
    await page.screenshot({ path: info.outputPath('studio-loading.png'), fullPage: true });
    holdTracks = false;
    releaseTracks?.();
    await expect(page.getByRole('button', { name: 'Pause', exact: true })).toBeVisible();
    await page.getByRole('slider', { name: 'Playback position' }).fill('13');
    await page.getByRole('button', { name: 'A · V1' }).click();
    await expect(page.getByRole('button', { name: 'Pause', exact: true })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Crossfade to Combat' })).toHaveAttribute(
      'aria-pressed',
      'true',
    );
    await expect(page.getByRole('slider', { name: 'Volume' })).toHaveValue('0.4');
    expect(
      Number(await page.getByRole('slider', { name: 'Playback position' }).inputValue()),
    ).toBeGreaterThanOrEqual(13);
    await page.getByRole('button', { name: 'Pause', exact: true }).click();
  }

  await page.getByRole('button', { name: 'Follow latest generation' }).click();
  await expect(page.getByRole('heading', { name: 'Version 2', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Revise this version' }).click();
  await page.getByLabel('Your idea or next change').fill('Keep the melody and soften the drums.');
  await page.reload();
  await expect(page.getByLabel('Your idea or next change')).toHaveValue(
    'Keep the melody and soften the drums.',
  );
  await expect(page.getByRole('heading', { name: 'Refine your soundtrack' })).toBeVisible();
  if (narrow)
    await page
      .getByRole('tab', { name: /^Preview(?: ·.*)?$/ })
      .first()
      .click();
  expect(writes).toHaveLength(0);
  await expect(page.getByRole('button', { name: 'Publish to music library' })).toBeVisible();
  expect(await page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')).toBe(
    true,
  );
  const axe = await new AxeBuilder({ page })
    .include('.music-studio')

    .analyze();
  expect(axe.violations).toEqual([]);
  if (process.env['SCREENSHOT_DIR']) {
    mkdirSync(process.env['SCREENSHOT_DIR'], { recursive: true });
    await page.getByRole('heading', { name: 'AI Music Studio' }).scrollIntoViewIfNeeded();
    await page.screenshot({
      path: join(process.env['SCREENSHOT_DIR'], `music-studio-${info.project.name}.png`),
      fullPage: true,
    });
    await page.evaluate("document.documentElement.dataset.theme='dark'");
    await page.screenshot({
      path: join(process.env['SCREENSHOT_DIR'], `music-studio-${info.project.name}-dark.png`),
      fullPage: true,
    });
  }
  // A checkout return restores the project without treating a URL as payment proof
  // or automatically resubmitting a request whose outcome is unknown.
  const submission = {
    path: `/api/v1/music-studio/threads/${id}/generate`,
    body: {
      id: '44444444-4444-4444-8444-444444444444',
      settings: { pipeline: 'acoustic-v1', seed: 0 },
      parent: second,
    },
  };
  await page.evaluate(
    ({ account, id, submission }) => {
      sessionStorage.setItem(`music-studio-checkout:${account}`, id);
      sessionStorage.setItem(`music-studio-checkout-balance:${account}`, '2');
      sessionStorage.setItem(`music-studio-pending:${account}:${id}`, JSON.stringify(submission));
    },
    { account: seed.accounts.kestrel, id, submission },
  );
  await page.goto('/music-studio?payment=returned');
  await expect(page).toHaveURL(new RegExp(`/music-studio/${id}\\?payment=returned$`));
  await expect(
    page.getByText('Confirming your payment. Your credits appear once payment is confirmed.'),
  ).toBeVisible();
  await expect(page.getByLabel('Your idea or next change')).toHaveValue(
    'Keep the melody and soften the drums.',
  );
  await expect(page.getByRole('heading', { name: 'Refine your soundtrack' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Retry the same request' })).toBeVisible();
  expect(
    await page.evaluate(
      ({ account, id }) =>
        JSON.parse(sessionStorage.getItem(`music-studio-pending:${account}:${id}`) ?? 'null'),
      { account: seed.accounts.kestrel, id },
    ),
  ).toEqual(submission);
  expect(writes).toHaveLength(0);
});
