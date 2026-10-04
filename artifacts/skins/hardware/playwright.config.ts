import base from '../../../platform/apps/web/e2e/playwright.config.ts';
export default {
  ...base,
  testDir: '../../../platform/apps/web/e2e',
  outputDir: './browser-results',
  use: {
    ...base.use,
    headless: false,
    launchOptions: { args: ['--use-angle=gl', '--enable-gpu', '--disable-software-rasterizer'] },
  },
  webServer: { ...base.webServer, cwd: '../../../platform/apps/web' },
};
