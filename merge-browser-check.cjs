const path = require('node:path');
const fs = require('node:fs');
const root = path.resolve(__dirname, '../..');
const mode = process.argv[2] || 'serial';
const dpr = Number(process.argv[3] || 1);
const steps = dpr > 1 ? 6 : 18;
const output = path.join(__dirname, 'merge-browser-' + mode + (dpr === 1 ? '' : '-dpr' + dpr + '-crossfade'));
fs.mkdirSync(output, {recursive:true});
const { chromium } = require(path.join(root, 'browser/node_modules/playwright'));
const { clickMainMenu, clickCustomGameStart } = require(path.join(root, 'browser/tests/main-menu'));
(async () => {
  const browser = await chromium.launch({headless:true});
  const page = await browser.newPage({viewport:{width:1152,height:896}, deviceScaleFactor:dpr});
  const errors = []; page.on('pageerror', e => errors.push(e.message));
  const result = {mode, dpr, samples:[], errors};
  try {
    await page.goto('http://127.0.0.1:8786/?renderer=webgl2&threads=' + mode + '&gl-errors=1');
    await page.waitForFunction(() => glob2Diagnostics.snapshot().screen.includes('MainMenuScreen'), null, {timeout:180000});
    if (mode === 'serial') result.gpu = await page.evaluate(() => { const gl=document.querySelector('#canvas').getContext('webgl2'); const ext=gl?.getExtension('WEBGL_debug_renderer_info'); return gl ? gl.getParameter(ext ? ext.UNMASKED_RENDERER_WEBGL : gl.RENDERER) : null; });
    await clickMainMenu(page, 'custom');
    await page.waitForFunction(() => glob2Diagnostics.snapshot().screen.includes('CustomGameScreen'));
    await clickCustomGameStart(page);
    await page.waitForFunction(() => glob2Diagnostics.snapshot().tick > 25, null, {timeout:180000});
    await page.locator('#canvas').focus();
    await page.mouse.move(500,450);
    const sample = () => page.evaluate(() => ({time:performance.now(), ...glob2Diagnostics.snapshot()}));
    for (let i=0;i<steps;i++) {
      if (i) await page.mouse.wheel(0, dpr > 1 ? (i === 1 ? 2600 : 200) : 100);
      const beforeZoom=await sample();
      await page.waitForFunction(frames => glob2Diagnostics.snapshot().frames > frames + 2, beforeZoom.frames, {timeout:60000});
      const before=await sample();
      await page.waitForTimeout(1500);
      const after=await sample();
      result.samples.push({step:i, frames:after.frames-before.frames, elapsedMs:after.time-before.time, state:after});
      console.log(mode, i, after.frames-before.frames, after.executionMode, after.renderContext?.error);
      if (dpr > 1 || i%3===0) await page.screenshot({path:path.join(output, `zoom-${i}.png`)});
    }
    await page.mouse.wheel(0, dpr > 1 ? -3400 : -1700);
    await page.waitForTimeout(1500);
    await page.screenshot({path:path.join(output, 'zoom-restored.png')});
    if (result.samples.some(x => x.state.executionMode !== mode || x.state.renderer !== 'webgl2' || x.frames <= 0)) throw new Error('Runtime did not keep rendering in requested mode');
    if (result.samples.some(x => x.state.renderContext?.error)) throw new Error('WebGL error');
    if (errors.length) throw new Error(errors.join('\n'));
  } finally {
    fs.writeFileSync(path.join(output,'check.json'), JSON.stringify(result,null,2));
    await browser.close();
  }
})().catch(e => { console.error(e); process.exitCode=1; });
