import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    // `npm run dev -w @glob2/web` against a local api on :8080.
    proxy: {
      '/api': 'http://localhost:8080',
      '/realtime': { target: 'ws://localhost:8080', ws: true },
    },
  },
  build: { outDir: 'dist', sourcemap: true },
});
