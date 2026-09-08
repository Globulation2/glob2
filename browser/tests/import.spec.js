const {test, expect} = require('@playwright/test');
const {gameURL} = require('./game-url');
const fs = require('node:fs/promises');
const path = require('node:path');
const {createHash} = require('node:crypto');
const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const click = (page,x,y) => page.locator('#canvas').click({position:{x,y},delay:80});
const menu = (page,x,y) => click(page,x+280,y+210);
const digest = bytes => ({size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});
async function select(page,name,bytes) {
  const chooser = page.waitForEvent('filechooser');
  await menu(page,60,485);
  await (await chooser).setFiles({name,mimeType:'application/octet-stream',buffer:bytes});
}
async function imported(page) { await expect.poll(async () => (await state(page)).import).toBe('succeeded'); }
async function chooseSave(page,name) {
  const names=await page.evaluate(() => glob2Diagnostics.saves());
  const index=names.indexOf(name);
  expect(index).toBeGreaterThanOrEqual(0);
  await menu(page,100,70+16*index);
}
async function exportedSave(page) {
  await menu(page,480,200); await screen(page,'CustomGameScreen');
  await menu(page,100,70); await menu(page,530,380);
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
  await page.locator('#canvas').press('p',{delay:80});
  await expect.poll(async () => (await state(page)).paused).toBe(true);
  await page.locator('#canvas').press('Escape',{delay:80});
  await click(page,600,400); await click(page,600,515);
  await page.locator('#canvas').press('Home');
  for(let i=0;i<50;i++) await page.locator('#canvas').press('Delete');
  await page.locator('#canvas').pressSequentially('Original');
  await click(page,520,555);
  await expect.poll(() => page.evaluate(() => glob2Diagnostics.saveDigest('Original.game'))).not.toBeNull();
  await expect.poll(async () => (await state(page)).persistence).toBe('persisted');
  await page.reload(); await screen(page,'MainMenuScreen');
  await menu(page,160,200); await screen(page,'ChooseMapScreen'); await chooseSave(page,'Original.game');
  const download = page.waitForEvent('download'); await menu(page,340,320);
  const file=await download;
  expect(file.suggestedFilename()).toBe('Original.game');
  return fs.readFile(await file.path());
}
test.beforeEach(async ({page}) => { await page.goto(gameURL()); await screen(page,'MainMenuScreen'); });

test('imports an exported save, preserves duplicate names, rejects corruption and reloads the imported game', async ({page},info) => {
  const errors=[]; page.on('pageerror',error=>errors.push(String(error)));
  const bytes=await exportedSave(page), expected=digest(bytes);
  await select(page,'Original.game',bytes); await imported(page);
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original.game'))).toEqual(expected);
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game'))).toEqual(expected);
  await page.screenshot({path:info.outputPath('imported-save.png')});
  const badOffset=Buffer.from(bytes), offsetField=4+bytes.readUInt32BE(0)+12;
  badOffset.writeUInt32BE(bytes.readUInt32BE(offsetField)+1,offsetField);
  for(const corrupt of [badOffset,bytes.subarray(0,3),bytes.subarray(0,bytes.length-1),Buffer.concat([bytes,Buffer.from('extra')])]) {
    await select(page,'Corrupt.game',corrupt);
    await expect.poll(async () => (await state(page)).import).toBe('invalid');
    expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Corrupt.game'))).toBeNull();
  }
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game'))).toEqual(expected);
  await menu(page,160,200); await screen(page,'ChooseMapScreen');
  await chooseSave(page,'Original_(1).game'); await menu(page,530,380);
  await expect.poll(async () => (await state(page)).screen).toBe('match');
  const loaded=(await state(page)).tick;
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(loaded+25);
  expect(errors).toEqual([]);
});

test('imports a custom map and starts it through the normal setup screen', async ({page},info) => {
  const bytes=await fs.readFile(path.resolve(__dirname,'../../maps/balanced.map'));
  await menu(page,480,200); await screen(page,'CustomGameScreen');
  await select(page,'Imported.map',bytes); await imported(page);
  expect(await page.evaluate(() => glob2Diagnostics.mapDigest('Imported.map'))).toEqual(digest(bytes));
  await page.screenshot({path:info.outputPath('imported-map.png')});
  const download=page.waitForEvent('download'); await menu(page,155,485);
  expect(digest(await fs.readFile(await (await download).path()))).toEqual(digest(bytes));
  await menu(page,530,380);
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
});

test('imports a complete replay and rejects a truncated command stream', async ({page}) => {
  const bytes=await fs.readFile(path.resolve(__dirname,'../../tests/baselines/cross-replay.replay'));
  await menu(page,160,200); await screen(page,'ChooseMapScreen'); await menu(page,340,440);
  await select(page,'Broken.replay',bytes.subarray(0,bytes.length-1));
  await expect.poll(async () => (await state(page)).import).toBe('invalid');
  await select(page,'Imported.replay',bytes); await imported(page);
  expect(await page.evaluate(() => glob2Diagnostics.replayDigest('Imported.replay'))).toEqual(digest(bytes));
  const download=page.waitForEvent('download'); await menu(page,340,320);
  expect(digest(await fs.readFile(await (await download).path()))).toEqual(digest(bytes));
  await menu(page,530,380);
  await expect.poll(async () => (await state(page)).screen).toBe('match');
  const loaded=(await state(page)).tick;
  await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(loaded+25);
});

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
  await select(page,'Original.game',bytes);
  await expect.poll(async () => (await state(page)).import).toBe('failed');
  await page.screenshot({path:info.outputPath('import-persistence-failure.png')});
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original.game'))).toEqual(digest(bytes));
  const download=page.waitForEvent('download'); await menu(page,340,320);
  expect(digest(await fs.readFile(await (await download).path()))).toEqual(digest(bytes));
  const restored=await context.newPage();
  await restored.goto(gameURL()); await screen(restored,'MainMenuScreen');
  expect(await restored.evaluate(() => glob2Diagnostics.saveDigest('Original.game'))).toEqual(digest(bytes));
  expect(await restored.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game'))).toBeNull();
  await restored.close();
  await page.evaluate(() => window.importQuota=false);
  await menu(page,60,485); await imported(page);
  await page.reload(); await screen(page,'MainMenuScreen');
  expect(await page.evaluate(() => glob2Diagnostics.saveDigest('Original_(1).game'))).toEqual(digest(bytes));
});
