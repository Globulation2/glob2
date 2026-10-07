import { fileURLToPath } from 'node:url';
import { defineConfig, searchForWorkspaceRoot } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    fs: {
      // Share native shader and icon sources without exposing the rest of the checkout.
      allow: [
        searchForWorkspaceRoot(fileURLToPath(new URL('.', import.meta.url))),
        fileURLToPath(new URL('../../../libgag/shaders', import.meta.url)),
        fileURLToPath(new URL('../../../datasrc/icons/tabler', import.meta.url)),
      ],
    },
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
    // `npm run dev -w @glob2/web` against a local api on :8080.
    proxy: {
      '/play': { target: 'http://localhost:8765', rewrite: (path) => path.replace(/^\/play/, '') },
      '/api': 'http://localhost:8080',
      '/realtime': { target: 'ws://localhost:8080', ws: true },
    },
  },
  build: { outDir: 'dist', sourcemap: true },
});
