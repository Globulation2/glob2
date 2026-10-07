const {editTextField,clickControl,clickListRow}=require('./main-menu');
const {test, expect} = require('@playwright/test');
const {clickMainMenu,gameURL,clickCustomGameStart}=require('./main-menu');
const fs = require('node:fs/promises');
const path = require('node:path');
const {createHash} = require('node:crypto');
const {gunzipSync,gzipSync} = require('node:zlib');
const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const digest = bytes => ({size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});
async function select(page,name,bytes) {
  const chooser = page.waitForEvent('filechooser');
  await clickControl(page,'import');
  await (await chooser).setFiles({name,mimeType:'application/octet-stream',buffer:bytes});
}
async function imported(page) { await expect.poll(async () => (await state(page)).import).toBe('succeeded'); }
async function chooseSave(page,name) {
  const names=await page.evaluate(() => glob2Diagnostics.saves());
  const index=names.indexOf(name);
  expect(index).toBeGreaterThanOrEqual(0);
  await clickListRow(page,'files',index);
}
async function exportedSave(page) {
  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickCustomGameStart(page); // The lobby prepares its selected generated landscape.
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('p',{delay:80});
  await expect.poll(async () => (await state(page)).paused).toBe(true);
  await page.locator('#canvas').press('Escape',{delay:80});
  await clickControl(page,'save');
  await editTextField(page,'Original');
  await clickControl(page,'ok');
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.saveDigest('Original.game.gz'))).not.toBeNull();
  await expect.poll(async () => (await state(page)).persistence).toBe('persisted');
  await page.reload(); await screen(page,'MainMenuScreen');
  await clickMainMenu(page,'load'); await screen(page,'ChooseMapScreen'); await chooseSave(page,'Original.game.gz');
  const download = page.waitForEvent('download'); await clickControl(page,'export');
  const file=await download;
  expect(file.suggestedFilename()).toBe('Original.game.gz');
  return fs.readFile(await file.path());
}
test.beforeEach(async ({page},info) => {
  const mode=info.title.match(/\((serial|threaded)\)$/)?.[1];
  const url=new URL(gameURL(),'http://localhost');
  if(mode) url.searchParams.set('threads',mode);
  await page.goto(url.pathname+url.search); await screen(page,'MainMenuScreen');
  if(mode) expect((await state(page)).executionMode).toBe(mode);
});

test('imports an exported save, preserves duplicate names, rejects corruption and reloads the imported game', async ({page},info) => {
  const errors=[]; page.on('pageerror',error=>errors.push(String(error)));
  const bytes=await exportedSave(page), expected=digest(bytes);
  await select(page,'Original.game.gz',bytes); await imported(page);
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original.game.gz'))).toEqual(expected);
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game.gz'))).toEqual(expected);
  await page.screenshot({path:info.outputPath('imported-save.png')});
  const raw=gunzipSync(bytes);
  const badOffset=Buffer.from(raw), offsetField=4+raw.readUInt32BE(0)+12;
  badOffset.writeUInt32BE(raw.readUInt32BE(offsetField)+1,offsetField);
  for(const corrupt of [gzipSync(badOffset),bytes.subarray(0,3),bytes.subarray(0,bytes.length-1),Buffer.concat([bytes,Buffer.from('extra')])]) {
    await select(page,'Corrupt.game.gz',corrupt);
    await expect.poll(async () => (await state(page)).import).toBe('invalid');
    expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Corrupt.game.gz'))).toBeNull();
  }
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game.gz'))).toEqual(expected);
  await clickMainMenu(page,'load'); await screen(page,'ChooseMapScreen');
  await chooseSave(page,'Original_(1).game.gz'); await clickControl(page,'ok');
  await expect.poll(async () => (await state(page)).screen).toBe('match');
  const loaded=(await state(page)).tick;
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(loaded+25);
  expect(errors).toEqual([]);
});

test('imports a custom map and starts it through the normal setup screen', async ({page},info) => {
  const bytes=await fs.readFile(path.resolve(__dirname,'../../maps/balanced.map.gz'));
  // CustomGameScreen has no import affordance of its own (#237 replaced its
  // map browsing with "Premade maps"/"Your maps" library tabs); importing a
  // map still only happens through the editor's "Load Map" chooser, which
  // writes into the same maps/ library the lobby's "Your maps" tab lists.
  await clickMainMenu(page,'editor'); await screen(page,'EditorMainMenu');
  await clickControl(page,'load-map'); await screen(page,'ChooseMapScreen');
  await select(page,'Imported.map.gz',bytes); await imported(page);
  expect(await page.evaluate(() => glob2Diagnostics.mapDigest('Imported.map.gz'))).toEqual(digest(bytes));
  await page.screenshot({path:info.outputPath('imported-map.png')});
  // Leave without loading it into the editor; only the library entry matters here.
  await page.locator('#canvas').press('Escape'); await screen(page,'EditorMainMenu');
  await page.locator('#canvas').press('Escape'); await screen(page,'MainMenuScreen');

  await clickMainMenu(page,'custom'); await screen(page,'CustomGameScreen');
  await clickControl(page,'map/mode/0'); // Premade maps.
  await clickControl(page,'map/library/1'); // "Your maps" library.
  await clickListRow(page,'map/list/1',0); // The imported map's row (only entry in a fresh profile).
  await clickCustomGameStart(page);
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
});

for(const mode of ['serial','threaded']) {
test(`imports a complete replay and rejects a truncated command stream (${mode})`, async ({page}) => {
  const bytes=await fs.readFile(path.resolve(__dirname,'fixtures/cross-replay.replay'));
  expect(bytes.subarray(4,16).toString()).toBe('replayHeader');
  expect(bytes.readUInt32BE(20)).toBe(141);
  await clickMainMenu(page,'load'); await screen(page,'ChooseMapScreen'); await clickControl(page,'switch');
  await select(page,'Broken.replay',bytes.subarray(0,bytes.length-1));
  await expect.poll(async () => (await state(page)).import).toBe('invalid');
  await select(page,'Imported.replay',bytes); await imported(page);
  expect(await page.evaluate(() => glob2Diagnostics.replayDigest('Imported.replay'))).toEqual(digest(bytes));
  const download=page.waitForEvent('download'); await clickControl(page,'export');
  expect(digest(await fs.readFile(await (await download).path()))).toEqual(digest(bytes));
  await clickControl(page,'ok');
  await expect.poll(async () => (await state(page)).screen).toBe('match');
  const loaded=(await state(page)).tick;
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(loaded+25);
});

}

test('failed import persistence offers export and retry without overwriting a save', async ({page,context},info) => {
  const bytes=await exportedSave(page);
  await page.evaluate(() => {
    window.importQuota = false;
    const put=IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put=function(...args) {
      if(window.importQuota) throw new DOMException('Injected quota exhaustion','QuotaExceededError');
      return put.apply(this,args);
    };
    window.importQuota=true;
  });
  await select(page,'Original.game.gz',bytes);
  await expect.poll(async () => (await state(page)).import).toBe('failed');
  await page.screenshot({path:info.outputPath('import-persistence-failure.png')});
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original.game.gz'))).toEqual(digest(bytes));
  const download=page.waitForEvent('download'); await clickControl(page,'export');
  expect(digest(await fs.readFile(await (await download).path()))).toEqual(digest(bytes));
  const restored=await context.newPage();
  await restored.goto(gameURL()); await screen(restored,'MainMenuScreen');
  expect(await restored.evaluate(() => glob2Diagnostics.saveDigest('Original.game.gz'))).toEqual(digest(bytes));
  expect(await restored.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game.gz'))).toBeNull();
  await restored.close();
  await page.evaluate(() => window.importQuota=false);
  await clickControl(page,'import'); await imported(page);
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game.gz'))).toEqual(digest(bytes));
});
