const {test, expect} = require('@playwright/test');

// Check the actual served release, not just the SCons flag list. Accidentally
// restoring Asyncify would otherwise let a blocking UI regression pass E2E.
test('browser runtime does not instrument stacks with Asyncify', async ({request}) => {
  const response = await request.get('/index.wasm');
  expect(response.ok()).toBeTruthy();
  const module = await WebAssembly.compile(await response.body());
  expect(WebAssembly.Module.exports(module).filter(entry => /asyncify/i.test(entry.name))).toEqual([]);
});
