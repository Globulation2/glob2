// SPDX-License-Identifier: GPL-3.0-or-later
const { test, expect } = require('@playwright/test');
const { gameURL, clickMainMenu, clickControl, clickByLabel, control } = require('./main-menu');
const archive = process.env.GLOB2_MUSIC_TEST_ARCHIVE;
async function start(page, runtime) {
  const url = new URL(gameURL(), 'http://localhost');
  url.searchParams.set('threads', runtime);
  url.searchParams.set('renderer', 'software');
  await page.goto(url.pathname + url.search);
  await expect
    .poll(() => page.evaluate(() => globalThis.glob2Diagnostics?.snapshot().screen))
    .toContain('MainMenuScreen');
  test.skip(
    runtime === 'threaded' && (await page.evaluate(() => Module.executionMode)) !== 'threaded',
    'Browser cannot run the threaded game.',
  );
  await clickMainMenu(page, 'settings');
  await clickControl(page, 'nav.1');
  // New browser profiles start muted. This is the real settings/activation path.
  await clickControl(page, 'audio.mute');
  await expect
    .poll(() => page.evaluate(() => Module.glob2Music.status.consumedFrames || 0))
    .toBeGreaterThan(48000);
  expect(await page.evaluate(() => Module.glob2Music.status.failed || false)).toBe(false);
}
for (const runtime of ['serial', 'threaded']) {
  test(`${runtime} game sends soundtrack and gain to independent audio`, async ({ page }, info) => {
    await start(page, runtime);
    await clickControl(page, 'audio.mute');
    expect(await page.evaluate(() => Module.glob2Music.gain)).toBe(0);
    await clickControl(page, 'audio.mute');
    expect(await page.evaluate(() => Module.glob2Music.gain)).toBeGreaterThan(0);
    await info.attach('game-audio-diagnostics', {
      body: JSON.stringify(await page.evaluate(() => Module.glob2Music.status)),
      contentType: 'application/json',
    });
  });
  test(`${runtime} imported preview controls reach independent audio`, async ({ page }, info) => {
    test.setTimeout(180000);
    test.skip(!archive, 'Set GLOB2_MUSIC_TEST_ARCHIVE to a valid music release ZIP.');
    await start(page, runtime);
    await page.route('**/api/v1/music**', (route) =>
      route.fulfill({ json: { items: [], next: null } }),
    );
    await clickControl(page, 'audio.library');
    await clickControl(page, 'music.import');
    const chooser = page.waitForEvent('filechooser');
    await clickControl(page, 'music.zip');
    await (await chooser).setFiles(archive);
    await clickControl(page, 'music.import.zip');
    await control(page, 'music.zip');
    await clickControl(page, 'back');
    await clickByLabel(page, /music.tabs/, 'Installed');
    let key;
    await expect
      .poll(() =>
        page
          .evaluate(() =>
            Object.keys(glob2Diagnostics.snapshot().controls).find((k) =>
              k.startsWith('music.play.'),
            ),
          )
          .then((k) => {
            key = k;
            return !!k;
          }),
      )
      .toBe(true);
    await clickControl(page, key);
    await expect.poll(() => page.evaluate(() => Module.glob2Music.status.preview)).toBe(true);
    await clickControl(page, 'music.play');
    await expect.poll(() => page.evaluate(() => Module.glob2Music.status.playing)).toBe(true);
    await clickControl(page, 'music.mood.2');
    await expect
      .poll(() => page.evaluate(() => Module.glob2Music.status.weights?.[2] || 0))
      .toBeGreaterThan(0.95);
    const beforePause = await page.evaluate(() => Module.glob2Music.status);
    await clickControl(page, 'music.play');
    await expect.poll(() => page.evaluate(() => Module.glob2Music.status.playing)).toBe(false);
    const afterPause = await page.evaluate(() => Module.glob2Music.status);
    const paused = afterPause.position;
    // The UI helper can take multiple frames to deliver the click under load.
    // Account for samples actually consumed during that time, including loops;
    // only a jump beyond consumption indicates discarded prepared music.
    const expectedPosition =
      (beforePause.position + (afterPause.consumedFrames - beforePause.consumedFrames) / 48000) %
      afterPause.duration;
    const distance = Math.abs(paused - expectedPosition);
    expect(Math.min(distance, afterPause.duration - distance)).toBeLessThan(0.05);
    await page.waitForTimeout(250);
    expect(await page.evaluate(() => Module.glob2Music.status.position)).toBe(paused);
    await clickControl(page, 'music.play');
    await expect.poll(() => page.evaluate(() => Module.glob2Music.status.playing)).toBe(true);
    await clickControl(page, 'back');
    await expect.poll(() => page.evaluate(() => Module.glob2Music.status.preview)).toBe(false);
    await info.attach('preview-audio-diagnostics', {
      body: JSON.stringify(await page.evaluate(() => Module.glob2Music.status)),
      contentType: 'application/json',
    });
  });
}
