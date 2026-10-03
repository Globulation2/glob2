# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: pacing.spec.js >> display-paced match preserves tick execution and paused input (threaded)
- Location: browser/tests/pacing.spec.js:33:3

# Error details

```
Error: expect(received).toBeGreaterThan(expected)

Expected: > 1500
Received:   809

Call Log:
- Timeout 90000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [ref=f1e1]:
  - generic [aria-hidden]:
    - strong: Globulation 2
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 20 of 20 MB
  - generic "Globulation 2" [active] [ref=f1e2]
```

# Test source

```ts
  1  | // SPDX-License-Identifier: GPL-3.0-or-later
  2  | const {test, expect} = require('@playwright/test');
  3  | const fs = require('node:fs');
  4  | const path = require('node:path');
  5  | const {openRuntimeHost} = require('./runtime-host');
  6  | const {gameURL, clickMainMenu, clickControl, clickListRow} = require('./main-menu');
  7  | const state = (page) => page.evaluate(() => glob2Diagnostics.snapshot());
  8  | const root = path.resolve(__dirname, '../..');
  9  | const fixture = path.join(root, 'games/cross-replay.game.gz');
  10 | 
  11 | // Compare the actual display-paced session with the serial CLI engine using
  12 | // the same initial state and no gameplay orders. Speed keys affect host pacing.
  13 | async function referenceTrace(page) {
  14 |   await openRuntimeHost(
  15 |     page,
  16 |     `<!doctype html><canvas id="canvas"></canvas><script>
  17 |     var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
  18 |       preRun:[()=>{ENV.GLOB2_REPLAY_PATH='/tmp/reference.replay';ENV.GLOB2_CHECKSUM_SIDECAR='1';
  19 |         FS.writeFile('/tmp/initial.game.gz',Uint8Array.from(atob('${fs.readFileSync(fixture).toString('base64')}'),c=>c.charCodeAt(0)));}],
  20 |       async onRuntimeInitialized(){
  21 |         const code=await Module.start(['--nox','/tmp/initial.game.gz','1500','1','--ai-threads','1']);
  22 |         if(code!==0)throw new Error('Reference engine exited: '+code);
  23 |         const bytes=FS.readFile('/tmp/reference.replay.checksums');let binary='';
  24 |         for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));
  25 |         window.trace=btoa(binary);}};
  26 |     </script><script src="/index.js"></script>`,
  27 |   );
  28 |   await page.waitForFunction(() => typeof window.trace === 'string');
  29 |   return Buffer.from(await page.evaluate(() => window.trace), 'base64');
  30 | }
  31 | 
  32 | for (const variant of ['serial', 'threaded']) {
  33 |   test(`display-paced match preserves tick execution and paused input (${variant})`, async ({page}, info) => {
  34 |     test.setTimeout(180000);
  35 |     const errors = [];
  36 |     page.on('pageerror', (error) => errors.push(String(error)));
  37 |     const reference = await referenceTrace(page);
  38 |     // A real HTTP document retains isolation for the worker runtime.
  39 |     const shell = fs
  40 |       .readFileSync(path.join(root, 'build/emscripten/client/release/index.html'), 'utf8')
  41 |       .replace(
  42 |         "ENV.HOME = '/home/web_user';",
  43 |         `ENV.HOME = '/home/web_user';
  44 |         ENV.GLOB2_REPLAY_PATH='/tmp/hosted.replay';ENV.GLOB2_CHECKSUM_SIDECAR='1';
  45 |         ENV.GLOB2_CHECKSUM_SIDECAR_MAX_TICKS='1500';`,
  46 |       );
  47 |     const url = new URL(gameURL(), 'http://localhost');
  48 |     url.searchParams.set('threads', variant);
  49 |     await openRuntimeHost(page, shell, url.search);
  50 |     await expect.poll(async () => (await state(page)).screen).toContain('MainMenuScreen');
  51 |     expect((await state(page)).executionMode).toBe(variant);
  52 |     await clickMainMenu(page, 'load');
  53 |     const chooser = page.waitForEvent('filechooser');
  54 |     await clickControl(page, 'import');
  55 |     await (await chooser).setFiles(fixture);
  56 |     await expect.poll(async () => (await state(page)).import).toBe('succeeded');
  57 |     await clickListRow(page, 'files', 0);
  58 |     await clickControl(page, 'ok');
  59 |     await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  60 |     for (let i = 0; i < 7; i++) await page.locator('#canvas').press('Control+=', {delay: 80});
> 61 |     await expect.poll(async () => (await state(page)).tick, {timeout: 90000}).toBeGreaterThan(1500);
     |                                                                               ^ Error: expect(received).toBeGreaterThan(expected)
  62 |     await page.locator('#canvas').press('p');
  63 |     await expect.poll(async () => (await state(page)).paused).toBe(true);
  64 |     const paused = await state(page);
  65 |     // Advancing display frames while ticks stay fixed is a functional assertion,
  66 |     // independent of the renderer's speed or the machine's refresh rate.
  67 |     await expect.poll(async () => (await state(page)).frames).toBeGreaterThan(paused.frames + 5);
  68 |     expect((await state(page)).tick).toBe(paused.tick);
  69 |     await page.locator('#canvas').press('Escape');
  70 |     await clickControl(page, 'quit');
  71 |     await expect.poll(async () => (await state(page)).screen).toContain('EndGameScreen');
  72 |     const trace = Buffer.from(
  73 |       await page.evaluate(() => {
  74 |         const bytes = Module.FS.readFile('/tmp/hosted.replay.checksums');
  75 |         let binary = '';
  76 |         for (let i = 0; i < bytes.length; i += 32768)
  77 |           binary += String.fromCharCode(...bytes.subarray(i, i + 32768));
  78 |         return btoa(binary);
  79 |       }),
  80 |       'base64',
  81 |     );
  82 |     fs.mkdirSync(info.outputDir, {recursive: true});
  83 |     fs.writeFileSync(info.outputPath('reference.checksums'), reference);
  84 |     fs.writeFileSync(info.outputPath('hosted.checksums'), trace);
  85 |     expect(trace.equals(reference)).toBe(true);
  86 |     expect(errors).toEqual([]);
  87 |   });
  88 | }
  89 | 
```