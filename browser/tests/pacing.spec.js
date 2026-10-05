// SPDX-License-Identifier: GPL-3.0-or-later
const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');
const {openRuntimeHost} = require('./runtime-host');
const {gameURL, clickMainMenu, clickControl, clickListRow, clickSettingsDone} = require('./main-menu');
const state = (page) => page.evaluate(() => glob2Diagnostics.snapshot());
const root = path.resolve(__dirname, '../..');
const fixture = path.join(root, 'games/cross-replay.game.gz');

// Compare the actual display-paced session with the serial CLI engine using
// the same initial state and no gameplay orders. Speed keys affect host pacing.
async function referenceTrace(page) {
  await openRuntimeHost(
    page,
    `<!doctype html><canvas id="canvas"></canvas><script>
    var Module={noInitialRun:true,canvas:document.getElementById('canvas'),
      preRun:[()=>{ENV.GLOB2_REPLAY_PATH='/tmp/reference.replay';ENV.GLOB2_CHECKSUM_SIDECAR='1';
        FS.writeFile('/tmp/initial.game.gz',Uint8Array.from(atob('${fs.readFileSync(fixture).toString('base64')}'),c=>c.charCodeAt(0)));}],
      async onRuntimeInitialized(){
        const code=await Module.start(['--nox','/tmp/initial.game.gz','1500','1','--ai-threads','1']);
        if(code!==0)throw new Error('Reference engine exited: '+code);
        const bytes=FS.readFile('/tmp/reference.replay.checksums');let binary='';
        for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));
        window.trace=btoa(binary);}};
    </script><script src="/index.js"></script>`,
  );
  await page.waitForFunction(() => typeof window.trace === 'string');
  return Buffer.from(await page.evaluate(() => window.trace), 'base64');
}

for (const variant of ['serial', 'threaded']) {
  test(`display-paced match preserves tick execution and paused input (${variant})`, async ({page}, info) => {
    test.setTimeout(180000);
    const errors = [];
    page.on('pageerror', (error) => errors.push(String(error)));
    const reference = await referenceTrace(page);
    // A real HTTP document retains isolation for the worker runtime.
    const shell = fs
      .readFileSync(path.join(root, 'build/emscripten/client/release/index.html'), 'utf8')
      .replace(
        "ENV.HOME = '/home/web_user';",
        `ENV.HOME = '/home/web_user';
        ENV.GLOB2_REPLAY_PATH='/tmp/hosted.replay';ENV.GLOB2_CHECKSUM_SIDECAR='1';
        ENV.GLOB2_CHECKSUM_SIDECAR_MAX_TICKS='1500';`,
      );
    const url = new URL(gameURL(), 'http://localhost');
    url.searchParams.set('threads', variant);
    await openRuntimeHost(page, shell, url.search);
    await expect.poll(async () => (await state(page)).screen).toContain('MainMenuScreen');
    expect((await state(page)).executionMode).toBe(variant);
    // Exercise different drawing ceilings against the same reference tick trace.
    await clickMainMenu(page, 'settings');
    await expect.poll(async () => (await state(page)).screen).toContain('SettingsScreen');
    await clickControl(page, 'graphics.fps');
    await clickControl(page, variant === 'serial' ? 'popup/0' : 'popup/4');
    await clickSettingsDone(page);
    await expect.poll(async () => (await state(page)).screen).toContain('MainMenuScreen');
    await clickMainMenu(page, 'load');
    const chooser = page.waitForEvent('filechooser');
    await clickControl(page, 'import');
    await (await chooser).setFiles(fixture);
    await expect.poll(async () => (await state(page)).import).toBe('succeeded');
    await clickListRow(page, 'files', 0);
    await clickControl(page, 'ok');
    await expect.poll(async () => (await state(page)).tick).toBeGreaterThan(25);
    for (let i = 0; i < 7; i++) await page.locator('#canvas').press('Control+=', {delay: 80});
    await expect.poll(async () => (await state(page)).tick, {timeout: 90000}).toBeGreaterThan(1500);
    await page.locator('#canvas').press('p');
    await expect.poll(async () => (await state(page)).paused).toBe(true);
    const paused = await state(page);
    // Advancing display frames while ticks stay fixed is a functional assertion,
    // independent of the renderer's speed or the machine's refresh rate.
    await expect.poll(async () => (await state(page)).frames).toBeGreaterThan(paused.frames + 5);
    expect((await state(page)).tick).toBe(paused.tick);
    const cadence = await page.evaluate(async () => {
      const start = performance.now(), frames = glob2Diagnostics.snapshot().frames;
      await new Promise(resolve => setTimeout(resolve, 1200));
      return {milliseconds:performance.now() - start, frames:glob2Diagnostics.snapshot().frames - frames};
    });
    const target = variant === 'serial' ? 25 : 120;
    fs.mkdirSync(info.outputDir, {recursive:true});
    fs.writeFileSync(info.outputPath('drawing-cadence.json'), JSON.stringify({target, ...cadence}, null, 2));
    expect(cadence.frames).toBeGreaterThan(0);
    expect(cadence.frames).toBeLessThanOrEqual(Math.ceil(target * cadence.milliseconds / 1000) + 2);
    await page.locator('#canvas').press('Escape');
    await clickControl(page, 'quit');
    await expect.poll(async () => (await state(page)).screen).toContain('EndGameScreen');
    const trace = Buffer.from(
      await page.evaluate(() => {
        const bytes = Module.FS.readFile('/tmp/hosted.replay.checksums');
        let binary = '';
        for (let i = 0; i < bytes.length; i += 32768)
          binary += String.fromCharCode(...bytes.subarray(i, i + 32768));
        return btoa(binary);
      }),
      'base64',
    );
    fs.mkdirSync(info.outputDir, {recursive: true});
    fs.writeFileSync(info.outputPath('reference.checksums'), reference);
    fs.writeFileSync(info.outputPath('hosted.checksums'), trace);
    expect(trace.equals(reference)).toBe(true);
    expect(errors).toEqual([]);
  });
}
