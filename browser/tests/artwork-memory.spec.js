// SPDX-License-Identifier: GPL-3.0-or-later
const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const {clickMainMenu, clickCustomGameStart} = require('./main-menu');

for (const variant of ['serial', 'threaded']) {
  test(`${variant} high-resolution startup fits the browser heap on a many-core device`, async ({page}, info) => {
    test.setTimeout(240000);
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    page.on('console', message => {
      if (/Cannot enlarge memory|Failed to grow the heap|Native allocation failed|Pthread .*error|GL_INVALID|GPU mip preparation failed/.test(message.text()))
        errors.push(message.text());
    });
    // Apply the same CPU report to the UI and every application/decoder realm.
    // Automatic sizing must retain working pools without allocating a stack
    // for every logical core on a workstation or server.
    await page.context().route('**/index.js', async route => {
      const response = await route.fetch();
      await route.fulfill({response, body:
        'Object.defineProperty(navigator,"hardwareConcurrency",{get:()=>64});\n' + await response.text()});
    });
    await page.goto(`/?renderer=webgl2&gl-errors=1&threads=${variant}`);
    const state = () => page.evaluate(() => glob2Diagnostics.snapshot());
    await expect.poll(async () => (await state()).screen).toContain('MainMenuScreen');
    // A warm optional package reproduces the large first-match allocations;
    // starting before it arrives would accidentally test original artwork.
    await expect.poll(async () => (await state()).assets.hd, {timeout:120000}).toBe('ready');
    await clickMainMenu(page, 'custom');
    await clickCustomGameStart(page);
    await expect.poll(async () => (await state()).frames, {timeout:120000}).toBeGreaterThan(30);
    await expect.poll(async () => (await state()).artworkReady, {timeout:120000}).toBe(true);
    const readyFrames = (await state()).frames;
    await expect.poll(async () => (await state()).frames).toBeGreaterThan(readyFrames + 5);
    const result = await state();
    const evidence = info.outputPath('artwork-memory.json');
    fs.writeFileSync(evidence, JSON.stringify(result, null, 2) + '\n');
    await info.attach('artwork-memory', {path:evidence, contentType:'application/json'});
    expect(result.executionMode).toBe(variant);
    if (variant === 'serial') expect(result.workerCount).toBe(0);
    else {
      // Seven compute workers and eight asset decoders; the asynchronous
      // SoundMixer loader may also be active while music changes.
      expect(result.workerCount).toBeGreaterThanOrEqual(15);
      expect(result.workerCount).toBeLessThanOrEqual(16);
    }
    expect(result.heapBytes).toBeGreaterThan(0);
    expect(result.heapBytes).toBeLessThan(1536 * 1024 * 1024);
    expect(result.renderContext.error).toBe(0);
    expect(errors).toEqual([]);
    await page.screenshot({path:info.outputPath('high-resolution-match.png')});
  });
}
