const {test, expect} = require('@playwright/test');
const {readFile} = require('node:fs/promises');
const {gameURL, clickMainMenu, clickControl, clickByLabel, control, readBrowserFile} = require('./main-menu');
const archive = process.env.GLOB2_MUSIC_TEST_ARCHIVE;
const state = page => page.evaluate(() => glob2Diagnostics.snapshot());
const screen = (page, name) => expect.poll(async () => (await state(page)).screen).toContain(name);
const directory = page => page.evaluate(() => {
  const root = '/home/web_user/.glob2/data/zik';
  return FS.analyzePath(root).exists ? FS.readdir(root).find(name => name.startsWith('community-')) : null;
});
async function library(page) {
  await clickMainMenu(page, 'settings');
  await screen(page, 'SettingsScreen');
  await clickControl(page, 'nav.1');
  await clickControl(page, 'audio.library');
  await screen(page, 'MusicLibraryScreen');
}
for (const fault of [false, true]) test(`music import persists across reload${fault ? ' after quota failure, export and retry' : ''}`, async ({page}, info) => {
  test.skip(!archive, 'Set GLOB2_MUSIC_TEST_ARCHIVE to an actual web library ZIP download.');
  await page.addInitScript(() => {
    window.musicStorageFault = false;
    const put = IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put = function(...args) {
      if (window.musicStorageFault) throw new DOMException('Injected quota exhaustion', 'QuotaExceededError');
      return put.apply(this, args);
    };
  });
  await page.route('**/api/v1/music**', route => route.fulfill({json: {items: [], next: null}}));
  await page.goto(gameURL());
  await screen(page, 'MainMenuScreen');
  await library(page);
  await clickControl(page, 'music.import');
  await screen(page, 'MusicImportScreen');
  const chooser = page.waitForEvent('filechooser');
  await clickControl(page, 'music.zip');
  await (await chooser).setFiles(archive);
  await control(page, 'music.import.zip');
  if (fault) await page.evaluate(() => window.musicStorageFault = true);
  await clickControl(page, 'music.import.zip');
  if (fault) {
    await control(page, 'music.retry');
    await expect.poll(async () => (await state(page)).persistence).toBe('failed');
    const download = page.waitForEvent('download');
    await clickControl(page, 'music.export');
    expect(await readFile(await (await download).path())).toEqual(await readFile(archive));
    await page.screenshot({path: info.outputPath('music-storage-recovery.png')});
    await page.evaluate(() => window.musicStorageFault = false);
    await clickControl(page, 'music.retry');
  }
  await expect.poll(async () => (await state(page)).persistence).toBe('persisted');
  await control(page, 'music.zip'); // The screen only accepts another file after its durable write completes.
  await expect.poll(() => directory(page)).toMatch(/^community-/);
  const name = await directory(page);
  const path = `/home/web_user/.glob2/data/zik/${name}/a1.opus`;
  const original = await readBrowserFile(page, path);
  await clickControl(page, 'back');
  await screen(page, 'MusicLibraryScreen');
  await clickByLabel(page, /music.tabs/, 'Installed');
  await clickControl(page, 'music.play.' + name);
  await screen(page, 'MusicSetScreen');
  await clickControl(page, 'music.primary');
  await screen(page, 'MusicLibraryScreen');
  await expect.poll(async () => (await state(page)).persistence).toBe('persisted');
  await page.reload();
  await screen(page, 'MainMenuScreen');
  expect(await readBrowserFile(page, path)).toEqual(original);
  await library(page);
  await clickByLabel(page, /music.tabs/, 'Installed');
  await control(page, 'music.use.' + name);
  await page.screenshot({path: info.outputPath('music-installed-after-reload.png')});
});
