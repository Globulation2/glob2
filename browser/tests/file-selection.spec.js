const {test, expect} = require('@playwright/test');
const path = require('node:path');

// Exercise real browser file inputs; selection is owned by the application host,
// independently of the screen that will validate and persist the selected bytes.
test.beforeEach(async ({page}) => {
  await page.setContent('<button id="import">Import</button>');
  await page.addScriptTag({path:path.resolve(__dirname, '../file-selection.js')});
  await page.evaluate(() => {
    document.querySelector('button').onclick = () => {
      window.selection = new Glob2FileSelection(['game']);
      selection.pick(document);
    };
  });
});

test('real file chooser returns exact bytes and releases its input', async ({page}) => {
  const chooser = page.waitForEvent('filechooser');
  await page.getByRole('button', {name:'Import'}).click();
  await (await chooser).setFiles({name:'backup.game', mimeType:'application/octet-stream', buffer:Buffer.from([0,255,1,128])});
  await expect.poll(() => page.evaluate(() => selection.state)).toBe('selected');
  expect(await page.evaluate(() => ({name:selection.file.name, bytes:[...selection.file.bytes]})))
    .toEqual({name:'backup.game', bytes:[0,255,1,128]});
  await expect(page.locator('input[type=file]')).toHaveCount(0);
});

test('file chooser accept filter does not bypass file type validation', async ({page}) => {
  const chooser = page.waitForEvent('filechooser');
  await page.getByRole('button', {name:'Import'}).click();
  await (await chooser).setFiles({name:'backup.exe', mimeType:'application/octet-stream', buffer:Buffer.from('invalid')});
  await expect.poll(() => page.evaluate(() => selection.state)).toBe('failed');
  expect(await page.evaluate(() => selection.file)).toBeNull();
  await expect(page.locator('input[type=file]')).toHaveCount(0);
});
