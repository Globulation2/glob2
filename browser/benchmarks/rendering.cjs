// Quick renderer comparison using real controls. Controlled release fixtures
// and longer measurements are still required for performance qualification.
const {chromium} = require('playwright');
(async () => {
  const renderer = process.argv[2];
  if (!['software','webgl2'].includes(renderer)) throw new Error('Pass software or webgl2');
  const args = process.env.GLOB2_ANGLE ? ['--use-angle=' + process.env.GLOB2_ANGLE] : [];
  const browser = await chromium.launch({headless:false, args});
  try {
    const page = await browser.newPage({viewport:{width:1200,height:900}});
    const url = new URL(process.env.GLOB2_TEST_URL || 'http://127.0.0.1:8770');
    url.searchParams.set('renderer', renderer);
    await page.goto(url.href);
    const screen = name => page.waitForFunction(name => glob2Diagnostics.snapshot().screen.includes(name), name);
    await screen('MainMenuScreen');
    const gpu = await page.evaluate(() => {
      const gl = document.querySelector('#canvas').getContext('webgl2');
      if (!gl) return null;
      const extension = gl.getExtension('WEBGL_debug_renderer_info');
      return extension ? gl.getParameter(extension.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
    });
    const click = (x,y) => page.locator('#canvas').click({position:{x,y},delay:80});
    await click(760,410); await screen('CustomGameScreen');
    await click(380,280); await click(810,590);
    await page.waitForFunction(() => glob2Diagnostics.snapshot().tick > 25, null, {timeout:60000});
    const sample = () => page.evaluate(() => ({time:performance.now(),...glob2Diagnostics.snapshot()}));
    const before = await sample();
    if (before.renderer !== renderer) throw new Error('Requested renderer is unavailable');
    await page.waitForTimeout(6000);
    const after = await sample();
    console.log(JSON.stringify({renderer,gpu,fixture:'first custom map, default options',
      elapsedMs:after.time-before.time,ticks:after.tick-before.tick,
      ticksPerSecond:(after.tick-before.tick)*1000/(after.time-before.time)},null,2));
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode=1; });
