const {test, expect} = require('@playwright/test');
const fs = require('node:fs');
const path = require('node:path');

// Run against the isolated platform e2e server (apps/web/e2e/server.ts), whose
// match fixture includes a frozen skin and a real accepted replay artifact.
test('online replay uses fresh signed match appearance without a preview override', async ({page, request}, info) => {
  test.skip(process.env.GLOB2_SKIN_REPLAY_API !== '1', 'Requires the seeded platform e2e server');
  test.setTimeout(240000);
  const seed = await (await request.get('/__seed')).json();
  const seen = [];
  page.on('request', request => seen.push(new URL(request.url()).pathname));
  const errors = [];
  page.on('pageerror', error => errors.push(String(error)));
  await page.addInitScript(() => {
    globalThis.replaySkinDraws = 0;
    const proto = WebGL2RenderingContext.prototype;
    const draw = proto.drawElements;
    const skins = new WeakSet(), inspected = new WeakSet();
    proto.drawElements = function(...args) {
      const program = this.getParameter(this.CURRENT_PROGRAM);
      if (program && !inspected.has(program)) {
        inspected.add(program);
        if (this.getAttachedShaders(program)?.some(shader => this.getShaderSource(shader)?.includes('surfaceNormal'))) skins.add(program);
      }
      if (skins.has(program)) globalThis.replaySkinDraws++;
      return draw.apply(this,args);
    };
  });
  // web-serial updates the runtime but packaging index.html also depends on
  // the threaded runtime. Exercise the current production shell with serial.
  const shell = fs.readFileSync(path.resolve(__dirname, '../shell.html'), 'utf8')
    .replace('{{{ SCRIPT }}}', '<script src="loader.js"></script>');
  await page.route(/\/play\/\?replay=/, route => route.fulfill({contentType:'text/html', body:shell}));
  await page.goto(`/play/?replay=${encodeURIComponent(`/api/v1/matches/${seed.featuredMatch}/artifacts/replay`)}&renderer=webgl2&gl-errors=1&threads=serial`);
  await expect.poll(() => page.evaluate(() => globalThis.glob2Diagnostics?.snapshot().watchReplay), {timeout:120000}).toBe('ready');
  await expect.poll(() => page.evaluate(() => globalThis.replaySkinDraws), {timeout:120000}).toBeGreaterThan(5);
  expect(seen).toContain(`/api/v1/matches/${seed.featuredMatch}/skins`);
  expect(seen).toContain('/.well-known/jwks.json');
  expect(seen.some(path => /^\/api\/v1\/skins\/versions\/[^/]+\/texture$/.test(path))).toBe(true);
  expect(await page.evaluate(() => glob2Diagnostics.snapshot().renderContext.error)).toBe(0);
  expect(errors).toEqual([]);
  await page.screenshot({path:info.outputPath('online-replay-skins.png')});
});
