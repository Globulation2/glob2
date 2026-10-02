const fs = require('fs');
const path = require('path');
const {chromium} = require('../../browser/node_modules/playwright');
(async () => {
  const browser = await chromium.launch();
  const log = [];
  try {
    for (const name of ['audio-baseline', 'net-baseline', 'audio', 'net']) {
      const page = await browser.newPage();
      page.on('console', message => log.push(name + ': ' + message.text()));
      page.on('pageerror', error => log.push(name + ': ' + String(error)));
      await page.goto('http://127.0.0.1:8778/' + name + '.html');
      await page.waitForFunction(() => window.result === 0 || window.failure, null, {timeout:30000});
      const result = await page.evaluate(() => ({result:window.result, failure:window.failure}));
      log.push(name + ': ' + JSON.stringify(result));
      if (name.endsWith('baseline') ? !result.failure : result.result !== 0)
        throw new Error('Unexpected result: ' + name);
      await page.close();
    }
  } finally {
    await browser.close();
    fs.writeFileSync(path.join(__dirname, 'results.log'), log.join('\n') + '\n');
    console.log(log.join('\n'));
  }
})().catch(error => { console.error(error); process.exit(1); });
