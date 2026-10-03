import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    include: ['packages/*/test/**/*.test.ts', 'apps/*/test/**/*.test.ts'],
    // Database tests share one Postgres server; each test file creates its own
    // database (see packages/db/test/support.ts), so files may run in parallel.
    testTimeout: 30_000,
    hookTimeout: 60_000,
  },
});
