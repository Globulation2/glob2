const fs = require('node:fs');
const path = require('node:path');
const {test, expect} = require('@playwright/test');
const {openRuntimeHost} = require('./runtime-host');
const {clickMainMenu, clickCustomGameStart} = require('./main-menu');

test('unskinned menu and match never request the optional mesh package', async ({page}) => {
  test.setTimeout(180000);
  const executionMode = process.env.GLOB2_SKIN_TEST_THREADS || 'serial';
  if (!['serial', 'threaded'].includes(executionMode)) throw new Error('Unsupported skin test execution mode');
  const requests = [], errors = [];
  page.on('request', request => requests.push(new URL(request.url()).pathname));
  page.on('pageerror', error => errors.push(String(error)));
  let shell = fs.readFileSync(path.resolve(__dirname, '../shell.html'), 'utf8')
    .replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>');
  shell = shell.replace('<head>', `<head><script>history.replaceState(null,'',location.pathname+'?renderer=webgl2&threads=${executionMode}');</script>`);
  await openRuntimeHost(page, shell);
  const state = () => page.evaluate(() => glob2Diagnostics.snapshot());
  await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('MainMenuScreen');
  expect(await page.evaluate(() => Module.executionMode)).toBe(executionMode);
  expect((await state()).assets.skins).toBe('idle');
  await clickMainMenu(page, 'custom');
  await expect.poll(async () => (await state()).screen).toContain('CustomGameScreen');
  await clickCustomGameStart(page);
  await expect.poll(async () => (await state()).screen, {timeout:120000}).toContain('match');
  await page.waitForTimeout(1000);
  expect((await state()).assets.skins).toBe('idle');
  expect(requests.some(url => /\/assets\/skins(?:-\d+)?\./.test(url))).toBe(false);
  expect((await state()).renderContext.error).toBe(0);
  expect(errors).toEqual([]);
});
