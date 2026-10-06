// Optional music coverage on all installed browser engines.
import { defineConfig, devices } from '@playwright/test';
import base from './playwright.config.ts';
export default defineConfig({
  ...base,
  testMatch: ['music.spec.ts', 'music-decoder.spec.ts', 'music-studio.spec.ts'],
  use: { ...base.use, video: 'on' },
  projects: [
    ...(base.projects ?? []),
    { name: 'firefox', use: { ...devices['Desktop Firefox'] } },
    { name: 'webkit', use: { ...devices['Desktop Safari'] } },
    { name: 'iphone', use: { ...devices['iPhone 13'] } },
  ],
});
