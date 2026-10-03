# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: skin-assets.spec.js >> unskinned menu and match never request the optional mesh package
- Location: browser/tests/skin-assets.spec.js:7:1

# Error details

```
Error: expect(received).toBe(expected) // Object.is equality

Expected: 0
Received: null
```

# Page snapshot

```yaml
- generic [ref=e1]:
  - generic [aria-hidden]:
    - strong: Globulation 2
    - status: Starting the game…
    - generic:
      - progressbar
      - generic: 4.5 of 4.5 MB
  - generic "Globulation 2" [active] [ref=e2]
```

# Test source

```ts
  1  | const fs = require('node:fs');
  2  | const path = require('node:path');
  3  | const {test, expect} = require('@playwright/test');
  4  | const {openRuntimeHost} = require('./runtime-host');
  5  | const {clickMainMenu, clickCustomGameStart} = require('./main-menu');
  6  | 
  7  | test('unskinned menu and match never request the optional mesh package', async ({page}) => {
  8  |   test.setTimeout(180000);
  9  |   const executionMode = process.env.GLOB2_SKIN_TEST_THREADS || 'serial';
  10 |   if (!['serial', 'threaded'].includes(executionMode)) throw new Error('Unsupported skin test execution mode');
  11 |   const requests = [], errors = [];
  12 |   page.on('request', request => requests.push(new URL(request.url()).pathname));
  13 |   page.on('pageerror', error => errors.push(String(error)));
  14 |   let shell = fs.readFileSync(path.resolve(__dirname, '../shell.html'), 'utf8')
  15 |     .replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>');
  16 |   shell = shell.replace('<head>', `<head><script>history.replaceState(null,'',location.pathname+'?renderer=webgl2&threads=${executionMode}');</script>`);
  17 |   await openRuntimeHost(page, shell);
  18 |   const state = () => page.evaluate(() => glob2Diagnostics.snapshot());
  19 |   await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('MainMenuScreen');
  20 |   expect(await page.evaluate(() => Module.executionMode)).toBe(executionMode);
  21 |   expect((await state()).assets.skins).toBe('idle');
  22 |   await clickMainMenu(page, 'custom');
  23 |   await expect.poll(async () => (await state()).screen).toContain('CustomGameScreen');
  24 |   await clickCustomGameStart(page);
  25 |   await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('match');
  26 |   await page.waitForTimeout(1000);
  27 |   expect((await state()).assets.skins).toBe('idle');
  28 |   expect(requests.some(url => /\/assets\/skins(?:-\d+)?\./.test(url))).toBe(false);
> 29 |   expect((await state()).renderContext.error).toBe(0);
     |                                               ^ Error: expect(received).toBe(expected) // Object.is equality
  30 |   expect(errors).toEqual([]);
  31 | });
  32 | 
```