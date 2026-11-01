// SPDX-License-Identifier: GPL-3.0-or-later
const { test, expect } = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const host = fs.readFileSync(path.join(__dirname, '../audio.js'), 'utf8');
// Exercise the production worker/worklet with a tiny host: no game render loop
// or renderer is needed to measure whether blocking that host starves playback.
for (const shared of [true, false])
  for (const rate of [44100, 48000])
    test(`${shared ? 'shared' : 'message'} music survives ${shared ? 2000 : 400} ms host stalls at ${rate} Hz`, async ({
      page,
    }, info) => {
      if (shared) {
        // Real HTTP isolation headers are required: WebKit's Playwright route.fulfill
        // reports isolation but hides SharedArrayBuffer. Navigate to a harmless
        // same-origin text resource, then replace its document with the test controls.
        await page.goto('/music-output.js');
        await page.setContent('<button id="start">Start music</button>');
      } else {
        await page.route('**/audio-harness.html', (route) =>
          route.fulfill({
            contentType: 'text/html',
            body: '<button id="start">Start music</button>',
          }),
        );
        await page.goto('/audio-harness.html');
      }
      test.skip(
        shared &&
          !(await page.evaluate(
            () => crossOriginIsolated && typeof SharedArrayBuffer === 'function',
          )),
        'This browser build has no shared memory; its MessagePort path is tested separately with a 400 ms stall.',
      );
      await page.evaluate((rate) => {
        const Context = window.AudioContext;
        window.AudioContext = class extends Context {
          constructor(options) {
            super({ ...options, sampleRate: rate });
          }
        };
        window.HEAPU8 = new Uint8Array(8192);
        window.Module = {
          _malloc: () => 8,
          _free: () => {},
          _glob2_audio_capture: () => {},
          printErr: (message) => console.error(message),
        };
      }, rate);
      await page.addScriptTag({ content: host });
      const pcm = fs.readFileSync(path.join(__dirname, '../../data/zik/original/a1.opus'));
      await page.evaluate((bytes) => {
        Module.glob2Music.send({ type: 'load', index: 0, bytes: Uint8Array.from(bytes).buffer });
        Module.glob2Music.send({ type: 'select', index: 0, value: false });
        document.querySelector('#start').onclick = () =>
          Module.glob2Music.volume(255, false, performance.now() * 1000);
      }, Array.from(pcm));
      await page.click('#start');
      await expect
        .poll(() => page.evaluate(() => Module.glob2Music.status.consumedFrames || 0))
        .toBeGreaterThan(48000);
      const before = await page.evaluate(() => Module.glob2Music.status);
      await page.evaluate(
        (ms) => {
          const end = performance.now() + ms;
          while (performance.now() < end) Math.sqrt(Math.random());
        },
        shared ? 2000 : 400,
      );
      await expect
        .poll(() => page.evaluate(() => Module.glob2Music.status.consumedFrames))
        .toBeGreaterThan(before.consumedFrames + (shared ? 80000 : 16000));
      const after = await page.evaluate(() => Module.glob2Music.status);
      expect(after.underruns, JSON.stringify({ before, after })).toBe(before.underruns);
      expect(after.starvationFrames, JSON.stringify({ before, after })).toBe(
        before.starvationFrames,
      );
      await page.evaluate(() => {
        Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
        document.dispatchEvent(new Event('visibilitychange'));
      });
      await expect
        .poll(() => page.evaluate(() => Module.glob2Music.context.state))
        .toBe('suspended');
      await page.evaluate(() => {
        Object.defineProperty(document, 'hidden', { configurable: true, get: () => false });
        document.dispatchEvent(new Event('visibilitychange'));
      });
      await page.click('#start');
      await expect.poll(() => page.evaluate(() => Module.glob2Music.context.state)).toBe('running');
      await expect
        .poll(() => page.evaluate(() => Module.glob2Music.status.consumedFrames))
        .toBeGreaterThan(after.consumedFrames + 48000);
      await info.attach('audio-diagnostics', {
        body: JSON.stringify(await page.evaluate(() => Module.glob2Music.status)),
        contentType: 'application/json',
      });
      await page.evaluate(() => Module.glob2Music.destroy());
    });

test('worker stalls consume the cushion and recover once after exhaustion', async ({
  page,
}, info) => {
  await page.route('**/audio-harness.html', (route) =>
    route.fulfill({ contentType: 'text/html', body: '<button>Start music</button>' }),
  );
  await page.route('**/music-worker.js', async (route) => {
    const response = await route.fetch();
    await route.fulfill({
      response,
      body:
        (await response.text()) +
        `
      const productionMessage=onmessage;
      onmessage=e=>{if(e.data.testStall){const end=performance.now()+e.data.testStall;while(performance.now()<end){};postMessage({testStalled:true});}else productionMessage(e);};
    `,
    });
  });
  await page.goto('/audio-harness.html');
  await page.evaluate(() => {
    window.HEAPU8 = new Uint8Array(8192);
    window.Module = {
      _malloc: () => 8,
      _free: () => {},
      _glob2_audio_capture: () => {},
      printErr: console.error,
    };
  });
  await page.addScriptTag({ content: host });
  const pcm = fs.readFileSync(path.join(__dirname, '../../data/zik/original/a1.opus'));
  await page.evaluate((bytes) => {
    Module.glob2Music.send({ type: 'load', index: 0, bytes: Uint8Array.from(bytes).buffer });
    Module.glob2Music.send({ type: 'select', index: 0, value: false });
    document.querySelector('button').onclick = () =>
      Module.glob2Music.volume(255, false, performance.now() * 1000);
  }, Array.from(pcm));
  await page.click('button');
  await expect
    .poll(() => page.evaluate(() => Module.glob2Music.status.consumedFrames || 0))
    .toBeGreaterThan(48000);
  for (const ms of [100, 250, 400, 1500]) {
    // Wait for at least the 20-block refill cushion before injecting a stall.
    await expect
      .poll(() => page.evaluate(() => Module.glob2Music.status.queuedFrames))
      .toBeGreaterThanOrEqual(20 * 1024);
    const before = await page.evaluate(() => Module.glob2Music.status);
    await page.evaluate(
      (ms) =>
        new Promise((resolve) => {
          const worker = Module.glob2Music.worker;
          const done = (e) => {
            if (e.data.testStalled) {
              worker.removeEventListener('message', done);
              resolve();
            }
          };
          worker.addEventListener('message', done);
          worker.postMessage({ testStall: ms });
        }),
      ms,
    );
    await expect
      .poll(() => page.evaluate(() => Module.glob2Music.status.consumedFrames))
      .toBeGreaterThan(before.consumedFrames + 48000);
    const after = await page.evaluate(() => Module.glob2Music.status);
    expect(after.underruns - before.underruns).toBe(ms < 500 ? 0 : 1);
    if (ms < 500)
      expect(after.starvationFrames, JSON.stringify({ before, after })).toBe(
        before.starvationFrames,
      );
  }
  await info.attach('worker-stall-diagnostics', {
    body: JSON.stringify(await page.evaluate(() => Module.glob2Music.status)),
    contentType: 'application/json',
  });
  await page.evaluate(() => Module.glob2Music.destroy());
});
