import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
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
