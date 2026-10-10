# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: artwork-memory.spec.js >> threaded high-resolution startup fits the browser heap on a many-core device
- Location: tests/artwork-memory.spec.js:7:3

# Error details

```
Error: expect(received).toBe(expected) // Object.is equality

Expected: "threaded"
Received: "serial"
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [aria-hidden]:
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 42 of 42 MB
    - combobox
  - generic "Globulation 2" [active] [ref=e2]
```

# Test source

```ts
  1  | // SPDX-License-Identifier: GPL-3.0-or-later
  2  | const {test, expect} = require('@playwright/test');
  3  | const fs = require('node:fs');
  4  | const {clickMainMenu, clickCustomGameStart} = require('./main-menu');
  5  | 
  6  | for (const variant of ['serial', 'threaded']) {
  7  |   test(`${variant} high-resolution startup fits the browser heap on a many-core device`, async ({page}, info) => {
  8  |     test.setTimeout(240000);
  9  |     const errors = [];
  10 |     page.on('pageerror', error => errors.push(String(error)));
  11 |     page.on('console', message => {
  12 |       if (/Cannot enlarge memory|Failed to grow the heap|Native allocation failed|Pthread .*error|GL_INVALID|GPU mip preparation failed/.test(message.text()))
  13 |         errors.push(message.text());
  14 |     });
  15 |     // Apply the same CPU report to the UI and every application/decoder realm.
  16 |     // Automatic sizing must retain working pools without allocating a stack
  17 |     // for every logical core on a workstation or server.
  18 |     await page.context().route('**/index.js', async route => {
  19 |       const response = await route.fetch();
  20 |       await route.fulfill({response, body:
  21 |         'Object.defineProperty(navigator,"hardwareConcurrency",{get:()=>64});\n' + await response.text()});
  22 |     });
  23 |     await page.goto(`/?renderer=webgl2&gl-errors=1&threads=${variant}`);
  24 |     const state = () => page.evaluate(() => glob2Diagnostics.snapshot());
  25 |     await expect.poll(async () => (await state()).screen).toContain('MainMenuScreen');
  26 |     // A warm optional package reproduces the large first-match allocations;
  27 |     // starting before it arrives would accidentally test original artwork.
  28 |     await expect.poll(async () => (await state()).assets.hd, {timeout:120000}).toBe('ready');
  29 |     await clickMainMenu(page, 'custom');
  30 |     await clickCustomGameStart(page);
  31 |     await expect.poll(async () => (await state()).frames, {timeout:120000}).toBeGreaterThan(30);
  32 |     await expect.poll(async () => (await state()).artworkReady, {timeout:120000}).toBe(true);
  33 |     const readyFrames = (await state()).frames;
  34 |     await expect.poll(async () => (await state()).frames).toBeGreaterThan(readyFrames + 5);
  35 |     const result = await state();
  36 |     const evidence = info.outputPath('artwork-memory.json');
  37 |     fs.writeFileSync(evidence, JSON.stringify(result, null, 2) + '\n');
  38 |     await info.attach('artwork-memory', {path:evidence, contentType:'application/json'});
> 39 |     expect(result.executionMode).toBe(variant);
     |                                  ^ Error: expect(received).toBe(expected) // Object.is equality
  40 |     if (variant === 'serial') expect(result.workerCount).toBe(0);
  41 |     else {
  42 |       // Seven compute workers and eight asset decoders; the asynchronous
  43 |       // SoundMixer loader may also be active while music changes.
  44 |       expect(result.workerCount).toBeGreaterThanOrEqual(15);
  45 |       expect(result.workerCount).toBeLessThanOrEqual(16);
  46 |     }
  47 |     expect(result.heapBytes).toBeGreaterThan(0);
  48 |     expect(result.heapBytes).toBeLessThan(1536 * 1024 * 1024);
  49 |     expect(result.renderContext.error).toBe(0);
  50 |     expect(errors).toEqual([]);
  51 |     await page.screenshot({path:info.outputPath('high-resolution-match.png')});
  52 |   });
  53 | }
  54 | 
```