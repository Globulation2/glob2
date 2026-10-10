// Browser smoke-test server: a fresh test database with a seeded match
// history (apps/api/test/historySeed.ts) plus showcase maps and an open room
// (showcase.ts), the API, and a front server that
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
import { seedAdminReports } from './adminShowcase.ts';
import { seedBuildingLibrary } from './buildingShowcase.ts';
import { previewFixture, seedShowcase } from './showcase.ts';
import { simVersionKey, type GeneratorSettings } from '@glob2/protocol';
import { SET_CREDITS_FIXTURE } from '../../api/test/setCreditsFixture.ts';

const here = import.meta.dirname;
const repo = resolve(here, '../../../..');
const dist = resolve(here, '../dist');
const port = Number(process.env['PORT'] ?? 4280);
const origin = `http://127.0.0.1:${port}`;
// Exercise the edge's actual policies, including dedicated-worker CSP, rather
// than letting browser smoke tests run with unrestricted script execution.
const edgeConfig = readFileSync(join(repo, 'deploy/Caddyfile'), 'utf8');
function edgePolicy(matcher: string): string {
  const policy = edgeConfig.match(
    new RegExp(`header @${matcher} Content-Security-Policy "([^"]+)"`),
  )?.[1];
  if (!policy) throw new Error(`Missing edge policy for ${matcher}`);
  return policy;
}
const webPolicy = edgePolicy('webApp');
const musicDecoderPolicy = edgePolicy('musicDecoder');
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
  '.webp': 'image/webp',
  '.mp4': 'video/mp4',
  '.svg': 'image/svg+xml',
  '.data': 'application/octet-stream',
  '.txt': 'text/plain',
  '.map': 'application/json',
};
const PLATFORM = /^\/(api\/|realtime$|signin(\/|$)|auth\/|\.well-known\/|j\/)/;

function sendFile(res: ServerResponse, path: string, isolated = false) {
  res.writeHead(200, {
    'content-type': TYPES[extname(path)] ?? 'application/octet-stream',
    'cache-control': 'no-cache',
    ...(isolated
      ? {
          'cross-origin-opener-policy': 'same-origin',
          'cross-origin-embedder-policy': 'require-corp',
        }
      : {}),
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
const musicRunner =
  process.env['MUSIC_E2E_PROCESSING'] === '1'
    ? await (async () => {
        const { startJobRunner, createLogger } = await import('@glob2/core');
        const { processMusic } = await import('../../music-worker/src/process.ts');
        return startJobRunner({
          pool: harness.database.pool,
          logger: createLogger('music-e2e', 'silent'),
          concurrency: 1,
          tasks: {
            'music-inspect': async (payload: unknown, helpers) =>
              processMusic(
                harness.database.db,
                harness.blobs,
                origin,
                (payload as { id: string }).id,
                true,
                helpers.job.attempts >= helpers.job.max_attempts,
              ),
            'music-convert': async (payload: unknown, helpers) =>
              processMusic(
                harness.database.db,
                harness.blobs,
                origin,
                (payload as { id: string }).id,
                false,
                helpers.job.attempts >= helpers.job.max_attempts,
              ),
          },
        });
      })()
    : undefined;
// Test credentials only. The end-to-end authoring flow never dispatches a model request.
if (process.env['GENERATOR_E2E_BINARY'] || process.env['GENERATOR_E2E_ENABLED'])
  process.env['GENERATOR_STUDIO_OPENAI_API_KEY'] = 'e2e-not-dispatched';
const api = await harness.start({
  origin,
  instance: {
    name: 'Glob2 Online (test)',
    // Full language sweeps reload every page twice; keep the fixture independent of throttling.
    limits: {
      apiPerMinute: 10_000,
      authPerMinute: 10_000,
      guestsPerHour: 10_000,
      signinAttemptsPerHour: 10_000,
      signinAttemptsPerMinuteTotal: 10_000,
    },
    queues: SEEDED_QUEUES,
    auth: { providers: [], local: { enabled: true } },
    ...(process.env['GENERATOR_E2E_BINARY'] || process.env['GENERATOR_E2E_ENABLED']
      ? {
          generatorStudio: {
            enabled: true,
            model: 'e2e-not-dispatched',
            rate: { version: 'e2e', input: 10, cachedInput: 1, output: 20 },
            maxRequestCredits: 100,
            maxOutputTokens: 2048,
          },
        }
      : {}),
  },
});
// Optional real isolated generator validation for the Studio end-to-end flow.
let generatorTimer: ReturnType<typeof setInterval> | undefined;
let generatorWork: Promise<void> | undefined;
if (process.env['GENERATOR_E2E_BINARY']) {
  const { execFile } = await import('node:child_process');
  const { promisify } = await import('node:util');
  const { createGeneratorExecutor } = await import('../../engine-agent/src/generatorValidation.ts');
  const { DEFAULT_LIMITS } = await import('../../engine-agent/src/engine.ts');
  const { putContent } = await import('@glob2/core');
  const { handleEngineJobResult, insertBlob } = await import('@glob2/play');
  const { stdout } = await promisify(execFile)(
    resolve(repo, process.env['GENERATOR_E2E_BINARY']),
    ['info', 'sim-version', '--format', 'json'],
    { cwd: repo },
  );
  const sim = JSON.parse(stdout);
  await harness.database.db
    .insertInto('engine_agents')
    .values({
      id: 'generator-studio-e2e',
      sim_version: simVersionKey(sim),
      kinds: ['validate-generator'],
      build: 'real-isolated',
    })
    .execute();
  const executor = await createGeneratorExecutor(
    {
      binary: resolve(repo, process.env['GENERATOR_E2E_BINARY']),
      workdir: resolve(repo, process.env['GENERATOR_E2E_WORKDIR'] ?? '.'),
      scratchRoot: resolve(
        repo,
        process.env['GENERATOR_E2E_SCRATCH'] ?? 'artifacts/generator-studio/isolated-ui',
      ),
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 64 * 1024 * 1024,
    },
    sim,
  );
  generatorTimer = setInterval(() => {
    if (generatorWork) return;
    generatorWork = (async () => {
      await harness.database.db
        .updateTable('engine_agents')
        .set({ last_seen_at: new Date() })
        .execute();
      const job = await harness.database.db
        .selectFrom('engine_jobs')
        .selectAll()
        .where('kind', '=', 'validate-generator')
        .where('status', '=', 'queued')
        .executeTakeFirst();
      if (!job) return;
      const payload = job.payload as {
        blobHash: string;
        example: GeneratorSettings;
      };
      const blob = await harness.database.db
        .selectFrom('blobs')
        .select('storage_key')
        .where('sha256', '=', payload.blobHash)
        .executeTakeFirstOrThrow();
      const stream = await harness.blobs.get(blob.storage_key);
      if (!stream) throw Error('Generator input blob missing.');
      const chunks: Buffer[] = [];
      for await (const chunk of stream) chunks.push(Buffer.from(chunk));
      const validation = await executor.validate(
        Buffer.concat(chunks),
        payload.example,
        new AbortController().signal,
      );
      for (const [bytes, type] of [
        [validation.canonical, 'application/x-glob2-generator'],
        [validation.png, 'image/png'],
      ] as const) {
        if (!bytes) continue;
        const stored = await putContent(harness.blobs, bytes);
        await insertBlob(harness.database.db, stored.sha256, stored.size, type, 'private');
        if (type === 'image/png') validation.report.previewHash = stored.sha256;
        else validation.report.fileHash = stored.sha256;
      }
      await handleEngineJobResult(harness.database.db, {
        jobId: job.id,
        kind: 'validate-generator',
        agent: 'studio-e2e-isolated',
        ok: true,
        result: validation.report,
      });
    })()
      .catch(console.error)
      .finally(() => {
        generatorWork = undefined;
      });
  }, 500);
}
const replayFixture = join(repo, 'browser/tests/fixtures/cross-replay.replay');
const seed = await seedHistory(harness.database.db, harness.blobs, {
  ...(existsSync(replayFixture) ? { replayBytes: readFileSync(replayFixture) } : {}),
  mapPreview: previewFixture('even-ground'),
});
await harness.database.db
  .updateTable('map_versions')
  .set({ set_credits: JSON.stringify(SET_CREDITS_FIXTURE) })
  .where('hash', '=', seed.mapHash)
  .execute();
// The test painter owns a designer unlock without contacting a payment provider.
await harness.database.db
  .insertInto('entitlements')
  .values({
    account_id: seed.accounts.kestrel,
    entitlement: 'skins:designer',
    source: 'browser-test',
  })
  .execute();
// A frozen appearance lets browser tests report the exact paint used in a match.
const paint = await harness.database.db
  .selectFrom('colony_skin_versions')
  .select(['id', 'building_color'])
  .executeTakeFirstOrThrow();
// Opt-in software-art fixture runs the real worker adapter against canonical preset paint.
if (process.env['SKIN_E2E_RENDER_BINARY']) {
  const { prepareJobQueue, enqueueSkinSprites, createLogger } = await import('@glob2/core');
  const { runProcess } = await import('@glob2/engine/process');
  const { renderSkin } = await import('../../skin-render-worker/src/process.ts');
  const binary = process.env['SKIN_E2E_RENDER_BINARY'];
  const probe = await runProcess({
    binary,
    cwd: repo,
    args: ['assets', 'skin-info', '--format', 'json'],
    limits: { timeoutMs: 10000 },
  });
  if (probe.code !== 0) throw new Error('Skin fixture renderer probe failed');
  const revision = (JSON.parse(probe.stdout) as { renderRevision: string }).renderRevision;
  const worker = harness.database.as('worker'),
    logger = createLogger('skin-e2e', 'silent');
  await prepareJobQueue(worker.pool, logger);
  await worker.db.insertInto('skin_render_revisions').values({ revision }).execute();
  await enqueueSkinSprites(worker.db, paint.id, revision);
  const derivative = await worker.db
    .selectFrom('colony_skin_sprites')
    .select('id')
    .where('version_id', '=', paint.id)
    .executeTakeFirstOrThrow();
  await renderSkin(
    worker.db,
    harness.blobs,
    { binary, cwd: repo, revision },
    derivative.id,
    logger,
  );
}
const seat = await harness.database.db
  .selectFrom('match_participants')
  .select(['account_id', 'team'])
  .where('match_id', '=', seed.featuredMatch)
  .where('account_id', 'is not', null)
  .orderBy('seat')
  .executeTakeFirstOrThrow();
if (!seat.account_id) throw new Error('Expected a human fixture participant');
await harness.database.db
  .insertInto('match_colony_skins')
  .values({
    match_id: seed.featuredMatch,
    team_index: seat.team,
    account_id: seat.account_id,
    version_id: paint.id,
    building_color: paint.building_color,
    assertion: 'refreshed-by-api',
  })
  .execute();
await harness.database.db
  .updateTable('matches')
  .set({ skins_frozen_at: new Date() })
  .where('id', '=', seed.featuredMatch)
  .execute();
await seedShowcase(harness.database.db, harness.blobs, seed);
await seedBuildingLibrary(harness.database.db, harness.blobs, seed);
await seedAdminReports(harness.database.db, seed);
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
    if (file) return sendFile(res, file, true);
    res.writeHead(404, { 'content-type': 'text/plain' });
    res.end('No browser game build here.');
    return;
  }
  const file = inside(dist, url.pathname === '/' ? 'index.html' : url.pathname);
  res.setHeader(
    'Content-Security-Policy',
    url.pathname === '/music/decode-worker.js' ? musicDecoderPolicy : webPolicy,
  );
  sendFile(
    res,
    file ?? join(dist, 'index.html'),
    /^\/(ai-studio|generator-studio)(\/|$)/.test(url.pathname) ||
      /^\/assets\/(editor|ts|json)\.worker-[^/]+\.js$/.test(url.pathname),
  );
});

front.listen(port, '127.0.0.1', () => {
  console.log(`READY ${origin}`);
});

const stop = async () => {
  front.close();
  await musicRunner?.stop();
  clearInterval(generatorTimer);
  await generatorWork;
  await harness.close();
  process.exit(0);
};
process.on('SIGINT', () => void stop());
process.on('SIGTERM', () => void stop());
