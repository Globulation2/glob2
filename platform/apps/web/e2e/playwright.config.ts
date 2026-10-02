// Browser smoke test of the built web app against a seeded API (server.ts).
//   npm run build -w @glob2/web && npm run e2e -w @glob2/web
// SCREENSHOT_DIR=<dir> also saves desktop and phone screenshots of each page.
import { defineConfig, devices } from '@playwright/test';

const port = Number(process.env['PORT'] ?? 4280);

export default defineConfig({
  testDir: '.',
  testMatch: '*.spec.ts',
  outputDir: '../../../../artifacts/web-app/test-results',
  timeout: 60_000,
  workers: 1,
  retries: 0,
  forbidOnly: Boolean(process.env['CI']),
  reporter: [['list']],
  use: { baseURL: `http://127.0.0.1:${port}`, trace: 'retain-on-failure' },
  projects: [
    {
      name: 'desktop',
      use: { ...devices['Desktop Chrome'], viewport: { width: 1280, height: 860 } },
    },
    { name: 'phone', use: { ...devices['Pixel 7'] } },
  ],
  webServer: {
    command: 'node e2e/server.ts',
    cwd: '..',
    url: `http://127.0.0.1:${port}/api/v1/instance`,
    reuseExistingServer: !process.env['CI'],
    timeout: 120_000,
    env: { PORT: String(port) },
  },
});
