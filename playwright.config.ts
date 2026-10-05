import config from '../../platform/apps/web/e2e/playwright.config.ts';
export default {
  ...config,
  testDir: '../../platform/apps/web/e2e',
  testMatch: 'map-preview.spec.ts',
  webServer: undefined,
  use: { ...config.use, baseURL: 'http://127.0.0.1:4281' },
};
