const {test, expect} = require('@playwright/test');
const {gameURL} = require('./main-menu');

test('loading page presents branded progress before the runtime is ready', async ({page}) => {
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  await page.route('**/index.wasm', async route => {
    await gate;
    await route.continue();
  });
  const navigation = page.goto(gameURL());
  await expect(page.locator('#loading')).toBeVisible();
  await expect(page.locator('#loading-status')).not.toBeEmpty();
  expect(await page.locator('body').evaluate(element => getComputedStyle(element).backgroundImage)).not.toBe('none');
  await expect(page.locator('#canvas')).toHaveCSS('opacity', '0');
  release();
  await navigation;
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.snapshot().screen)).toContain('MainMenuScreen');
  await expect(page.locator('#loading')).toHaveAttribute('aria-hidden', 'true');
  await expect(page.locator('#canvas')).toHaveCSS('opacity', '1');
});

// Check the actual served release, not just the SCons flag list. Accidentally
// restoring Asyncify would otherwise let a blocking UI regression pass E2E.
test('browser runtime does not instrument stacks with Asyncify', async ({request}) => {
  const response = await request.get('/index.wasm');
  expect(response.ok()).toBeTruthy();
  const module = await WebAssembly.compile(await response.body());
  expect(WebAssembly.Module.exports(module).filter(entry => /asyncify/i.test(entry.name))).toEqual([]);
});
