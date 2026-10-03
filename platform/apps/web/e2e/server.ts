// Browser smoke-test server: a fresh test database with a seeded match
// history (apps/api/test/historySeed.ts), the API, and a front server that
// plays the edge's part (deploy/Caddyfile): platform paths go to the API,
// /play/ serves the browser game build when one exists, and every other path
// serves the built web app with an index.html fallback.
//
//   node apps/web/e2e/server.ts            (after npm run build -w @glob2/web)
//
// Env: PORT (default 4280), GLOB2_WEB_CLIENT_DIR (browser game build, default
// <repo>/build/emscripten/client/release), SEED_OUT (write the seed ids as JSON).
// Needs the test Postgres of packages/db/test/support.ts.
import { createReadStream, existsSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { createServer, request as httpRequest, type ServerResponse } from 'node:http';
import { extname, join, normalize, resolve } from 'node:path';
import { SEEDED_QUEUES, seedHistory } from '../../api/test/historySeed.ts';
import { createHarness } from '../../api/test/support.ts';

const here = import.meta.dirname;
const repo = resolve(here, '../../../..');
const dist = resolve(here, '../dist');
const port = Number(process.env['PORT'] ?? 4280);
const origin = `http://127.0.0.1:${port}`;
const gameDir =
  process.env['GLOB2_WEB_CLIENT_DIR'] ?? join(repo, 'build/emscripten/client/release');

const TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
  '.woff2': 'font/woff2',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.data': 'application/octet-stream',
  '.txt': 'text/plain',
  '.map': 'application/json',
};
const PLATFORM = /^\/(api\/|realtime$|signin(\/|$)|auth\/|\.well-known\/|j\/)/;

function sendFile(res: ServerResponse, path: string) {
  res.writeHead(200, {
    'content-type': TYPES[extname(path)] ?? 'application/octet-stream',
    'cache-control': 'no-cache',
  });
  createReadStream(path).pipe(res);
}

function inside(root: string, path: string): string | undefined {
  const full = normalize(join(root, path));
  if (!full.startsWith(root)) return undefined;
  return existsSync(full) && statSync(full).isFile() ? full : undefined;
}

if (!existsSync(join(dist, 'index.html'))) {
  throw new Error('Build the web app first: npm run build -w @glob2/web');
}

const harness = await createHarness();
const api = await harness.start({
  origin,
  instance: {
    name: 'Glob2 Online (test)',
    queues: SEEDED_QUEUES,
    auth: { providers: [], local: { enabled: true } },
  },
});
const replayFixture = join(repo, 'browser/tests/fixtures/cross-replay.replay');
const seed = await seedHistory(harness.database.db, harness.blobs, {
  ...(existsSync(replayFixture) ? { replayBytes: readFileSync(replayFixture) } : {}),
});
if (process.env['SEED_OUT']) writeFileSync(process.env['SEED_OUT'], JSON.stringify(seed, null, 2));
const apiUrl = new URL(api.url);

const front = createServer((req, res) => {
  const url = new URL(req.url ?? '/', origin);
  if (url.pathname === '/__seed') {
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify(seed));
    return;
  }
  if (PLATFORM.test(url.pathname)) {
    const upstream = httpRequest(
      {
        host: apiUrl.hostname,
        port: apiUrl.port,
        method: req.method,
        path: req.url,
        headers: { ...req.headers, host: `127.0.0.1:${port}` },
      },
      (response) => {
        res.writeHead(response.statusCode ?? 502, response.headers);
        response.pipe(res);
      },
    );
    upstream.on('error', () => {
      res.writeHead(502);
      res.end();
    });
    req.pipe(upstream);
    return;
  }
  if (url.pathname === '/play') {
    res.writeHead(308, { location: '/play/' });
    res.end();
    return;
  }
  if (url.pathname.startsWith('/play/')) {
    const file = inside(gameDir, url.pathname.slice('/play/'.length) || 'index.html');
    if (file) return sendFile(res, file);
    res.writeHead(404, { 'content-type': 'text/plain' });
    res.end('No browser game build here.');
    return;
  }
  const file = inside(dist, url.pathname === '/' ? 'index.html' : url.pathname);
  sendFile(res, file ?? join(dist, 'index.html'));
});

front.listen(port, '127.0.0.1', () => {
  console.log(`READY ${origin}`);
});

const stop = async () => {
  front.close();
  await harness.close();
  process.exit(0);
};
process.on('SIGINT', () => void stop());
process.on('SIGTERM', () => void stop());
