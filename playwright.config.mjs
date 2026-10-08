import { defineConfig, devices } from '../../platform/node_modules/@playwright/test/index.mjs';
export default defineConfig({
  testDir: '.', testMatch: 'runner.spec.ts', outputDir: './playwright-results',
  workers: 1, reporter: 'list',
  use: { baseURL: 'http://127.0.0.1:5178' },
  projects: [
    { name:'desktop', use:{...devices['Desktop Chrome'], viewport:{width:1280,height:860}} },
    { name:'phone', use:{...devices['Pixel 7']} },
  ],
});
