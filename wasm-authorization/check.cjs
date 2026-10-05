const { createRequire } = require('node:module');
const fs = require('node:fs');
const path = require('node:path');
const { chromium, firefox, webkit } = createRequire(path.resolve('browser/package.json'))('@playwright/test');
(async () => {
  const results = [];
  for (const [name, engine] of Object.entries({ chromium, firefox, webkit })) {
    let browser;
    try {
      browser = await engine.launch({ headless: true });
      const page = await browser.newPage();
      const errors = [];
      page.on('pageerror', e => errors.push(String(e)));
      await page.goto('http://127.0.0.1:8775');
      await page.waitForFunction(() => window.authorizationResult, { timeout: 60000 });
      const result = await page.evaluate(() => window.authorizationResult);
      results.push({ name, ...result, errors });
    } catch (error) { results.push({name, error:String(error)}); }
    finally { if (browser) await browser.close(); }
  }
  fs.writeFileSync('artifacts/skin-studio/wasm-authorization/results.json', JSON.stringify(results,null,2));
  console.log(JSON.stringify(results,null,2));
  if (results.some(r => r.error || r.failed || r.outstanding || r.errors?.length)) process.exitCode = 1;
})();
