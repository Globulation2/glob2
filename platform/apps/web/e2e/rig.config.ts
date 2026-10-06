// Focused deformation conformance; independent of the built app and seeded API.
import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: '.',
  testMatch: 'rig-conformance.ts',
  outputDir: '../../../../artifacts/rig-conformance',
  timeout: 120_000,
  workers: 1,
  retries: 0,
  forbidOnly: Boolean(process.env['CI']),
  reporter: [['list']],
  use: { trace: 'retain-on-failure' },
  projects: [
    { name: 'chromium', use: { browserName: 'chromium' } },
    { name: 'firefox', use: { browserName: 'firefox' } },
    { name: 'webkit', use: { browserName: 'webkit' } },
  ],
});
