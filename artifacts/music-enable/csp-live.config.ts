import { defineConfig, devices } from '/Users/bradley/.codex/worktrees/5f0c/glob2/platform/node_modules/@playwright/test/index.mjs';
export default defineConfig({
  testDir: '/Users/bradley/.codex/worktrees/5f0c/glob2/platform/apps/web/e2e',
  testMatch: 'music-decoder.spec.ts',
  outputDir: '/Users/bradley/.codex/worktrees/5f0c/glob2/artifacts/music-enable/csp-live-results',
  timeout: 30000,
  workers: 1,
  reporter: [['list']],
  use: {baseURL:'https://app.glob2online.com', trace:'retain-on-failure'},
  projects: [
    {name:'desktop',use:{...devices['Desktop Chrome']}},
    {name:'phone',use:{...devices['Pixel 7']}},
  ],
});
