const path = require('node:path');
const {defineConfig} = require('../../platform/node_modules/@playwright/test');
const baseURL = 'http://127.0.0.1:4587';
module.exports = defineConfig({
  testDir: path.resolve(__dirname, '../../platform/apps/web/e2e'),
  testMatch: 'i18n.spec.ts',
  outputDir: path.resolve(__dirname, 'online-cross-browser-results'),
  timeout: 180000,
  expect: {timeout: 30000},
  workers: 1,
  retries: 0,
  reporter: [['list'], ['json', {outputFile: path.resolve(__dirname, 'checks/online-cross-browser-results.json')}]],
  projects: ['firefox', 'webkit'].flatMap(browserName => ['desktop', 'phone'].map(device => ({
    name: browserName + '-' + device,
    use: {browserName, baseURL,
      viewport: device === 'phone' ? {width: 390, height: 844} : {width: 1280, height: 900},
      hasTouch: device === 'phone', trace: 'retain-on-failure', screenshot: 'only-on-failure'},
  }))),
});
