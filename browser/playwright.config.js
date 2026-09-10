const {defineConfig} = require('@playwright/test');
const path = require('node:path');
module.exports = defineConfig({
  testDir: './tests',
  outputDir: '../build/browser-test-results',
  timeout: 90000,
  expect: {timeout: 30000},
  workers: 1,
  forbidOnly: Boolean(process.env.CI),
  retries: 0,
  reporter: [['list'], ['html', {outputFolder:'../build/browser-test-report', open:'never'}]],
  use: {
    baseURL: process.env.GLOB2_TEST_URL || 'http://127.0.0.1:8770',
    viewport: {width:1200, height:900},
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: ['chromium','firefox','webkit'].map(browserName => ({
    name:browserName,
    use:{browserName, ...(browserName === 'firefox' && process.env.GLOB2_FIREFOX_HEADED === '1' ? {headless:false} : {})},
  })),
  webServer: process.env.GLOB2_TEST_URL ? undefined : {
    command: 'python3 -m http.server 8770 --bind 127.0.0.1 --directory build/emscripten/client/release',
    cwd: path.resolve(__dirname, '..'),
    url: 'http://127.0.0.1:8770',
    reuseExistingServer: false,
  },
});
