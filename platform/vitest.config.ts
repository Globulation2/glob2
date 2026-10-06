import { fileURLToPath } from 'node:url';
import { searchForWorkspaceRoot } from 'vite';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  server: {
    fs: {
      // Web skin previews use the same shader source as the native renderer.
      allow: [
        searchForWorkspaceRoot(fileURLToPath(new URL('.', import.meta.url))),
        fileURLToPath(new URL('../libgag/shaders', import.meta.url)),
      ],
    },
  },
  test: {
    include: [
      'packages/*/test/**/*.test.ts',
      'apps/*/test/**/*.test.ts',
      'apps/web/test/**/*.test.tsx',
    ],
    // Database tests share one Postgres server; each test file creates its own
    // database (see packages/db/test/support.ts), so files may run in parallel.
    testTimeout: 30_000,
    hookTimeout: 60_000,
  },
});
