const path = require('node:path');
const root = path.resolve(__dirname, '../../../..');
const {defineConfig} = require(path.join(root, 'platform/node_modules/@playwright/test'));
module.exports = defineConfig({
  testDir: path.join(root, 'platform/apps/web/e2e'), testMatch: 'i18n.spec.ts',
  outputDir: path.join(__dirname, 'results'), timeout: 180000,
  expect: {timeout: 30000}, workers: 1, retries: 0,
  reporter: [['list'], ['json', {outputFile: path.join(__dirname, 'results.json')}]],
  projects: ['chromium','firefox','webkit'].flatMap(browserName => ['desktop','phone'].map(device => ({
    name: browserName + '-' + device,
    use: {browserName, baseURL: 'http://127.0.0.1:4587',
      viewport: device === 'phone' ? {width: 390, height: 844} : {width: 1280, height: 900},
      hasTouch: device === 'phone', trace: 'retain-on-failure', screenshot: 'only-on-failure'},
  }))),
});
