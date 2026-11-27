// Crash reports end to end (docs/multiplayer/crash-reports.md): symbol uploads,
// crash bundles, the crash worker's symbolication and grouping, and the
// admin views. The minidump case runs only where minidump-stackwalk is
// installed (MINIDUMP_STACKWALK, or the deployed image's path).
import { randomUUID } from 'node:crypto';
import { existsSync, mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { gzipSync } from 'node:zlib';
import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type {
  AdminCrashGroupList,
  AdminCrashReportDetail,
  AdminCrashReportList,
  CrashSymbolReceipt,
} from '@glob2/protocol';
import { createLogger } from '@glob2/core';
import { createHarness, type Harness, type Instance } from './support.ts';
import { seedHistory, SEEDED_QUEUES, type SeededHistory } from './historySeed.ts';
import { zip } from './zipBundle.ts';
import { authorized, parseModuleLine } from '../src/crashReports/symbols.ts';
import { processCrashReport } from '../../crash-worker/src/process.ts';
import { SymbolCache } from '../../crash-worker/src/symbols.ts';
import { retainCrashReports } from '../../worker/src/crashReportRetention.ts';

const TOKEN = 'release-workflow-token';
const VERSION = '0.11.8.1';
const FIXTURES = join(import.meta.dirname, '../../crash-worker/test/fixtures');
const STACKWALK = [process.env['MINIDUMP_STACKWALK'], '/usr/local/bin/minidump-stackwalk'].find(
  (path): path is string => !!path && existsSync(path),
);
const logger = createLogger('crash-test', 'silent');

let harness: Harness;
let api: Instance;
let unconfigured: Instance;
let seed: SeededHistory;
let cache: SymbolCache;
beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({
    origin: 'http://play.test',
    instance: { queues: SEEDED_QUEUES },
    secrets: { CRASH_SYMBOL_UPLOAD_TOKEN: TOKEN },
  });
  unconfigured = await harness.start({
    origin: 'http://play.test',
    instance: { queues: SEEDED_QUEUES },
  });
  seed = await seedHistory(harness.database.db, harness.blobs);
  cache = new SymbolCache(
    mkdtempSync(join(tmpdir(), 'glob2-symbols-')),
    harness.database.db,
    harness.blobs,
    1 << 30,
  );
});
afterAll(async () => harness?.close());

function upload(
  file: Buffer,
  query: string,
  token: string | null = TOKEN,
  instance: Instance = api,
) {
  return instance.app.inject({
    method: 'POST',
    url: `/api/v1/crash-symbols?${query}`,
    headers: {
      'content-type': 'application/gzip',
      ...(token ? { authorization: `Bearer ${token}` } : {}),
    },
    payload: file,
  });
}

const MAC_SYM = [
  'MODULE mac arm64 70B89F2716343580A69557CDB41D77430 glob2',
  'FILE 0 /Users/runner/work/glob2/glob2/src/unit/Unit.cpp',
  'FUNC 1000 40 0 Unit::step(int)',
  '1000 40 412 0',
  'FUNC 2000 40 0 Team::update()',
  '2000 40 90 0',
  '',
].join('\n');
const BUILD_ID = '9f86d081884c7d659a2feaa0c55ad015';

function context(kind: string, overrides: Record<string, unknown> = {}) {
  return {
    schema: 1,
    kind,
    installId: randomUUID(),
    submissionId: randomUUID(),
    version: VERSION,
    versionMinor: 219,
    simRevision: 170,
    distribution: 'app_store',
    platform: 'ios',
    os: 'iOS 26.1',
    deviceClass: 'phone',
    deviceTier: 'high',
    description: '',
    createdAt: '2026-11-26T12:00:00Z',
    ...overrides,
  };
}
function report(kind: string, entries: [string, Buffer | string][], overrides = {}) {
  return api.app.inject({
    method: 'POST',
    url: '/api/v1/crash-reports',
    headers: { 'content-type': 'application/zip' },
    remoteAddress: `198.51.100.${Math.floor(Math.random() * 200)}`,
    payload: zip([['context.json', JSON.stringify(context(kind, overrides))], ...entries]),
  });
}
function metrickit(offsets: number[]) {
  type Frame = Record<string, unknown>;
  let root: Frame | undefined;
  for (const offset of [...offsets].reverse()) {
    const frame: Frame = {
      binaryUUID: '70B89F27-1634-3580-A695-57CDB41D7743',
      binaryName: 'glob2',
      offsetIntoBinaryTextSegment: offset,
    };
    if (root) frame['subFrames'] = [root];
    root = frame;
  }
  return JSON.stringify({
    callStackTree: {
      callStackPerThread: true,
      callStacks: [{ threadAttributed: true, callStackRootFrames: [root] }],
    },
    diagnosticMetaData: { exceptionType: 1, signal: 11 },
  });
}
function admin<T>(url: string) {
  return api.app
    .inject({
      method: 'GET',
      url,
      headers: { cookie: `glob2_session=${seed.adminSession}`, origin: 'http://play.test' },
    })
    .then((r) => ({ status: r.statusCode, body: r.json<T>() }));
}
async function processReport(id: string) {
  await processCrashReport(
    harness.database.db,
    harness.blobs,
    {
      stackwalk: { binary: STACKWALK ?? '/nonexistent' },
      cache,
    },
    id,
    logger,
  );
  return harness.database.db
    .selectFrom('crash_reports')
    .selectAll()
    .where('id', '=', id)
    .executeTakeFirstOrThrow();
}

describe('symbol uploads', () => {
  it('reads the module from a Breakpad MODULE line', () => {
    expect(parseModuleLine('MODULE Linux x86_64 C207E70EB5F0F15275A7EEF73890DC9F0 glob2')).toEqual({
      os: 'Linux',
      arch: 'x86_64',
      debugId: 'C207E70EB5F0F15275A7EEF73890DC9F0',
      module: 'glob2',
    });
    expect(() =>
      parseModuleLine('MODULE Linux x86_64 C207E70EB5F0F15275A7EEF73890DC9F0 ..'),
    ).toThrow();
    expect(() => parseModuleLine('INFO CODE_ID 1234')).toThrow();
    expect(authorized(`Bearer ${TOKEN}`, TOKEN)).toBe(true);
    expect(authorized(`Bearer ${TOKEN}x`, TOKEN)).toBe(false);
    expect(authorized(undefined, TOKEN)).toBe(false);
  });

  it('takes symbols only from the release workflows, for release versions', async () => {
    const file = gzipSync(MAC_SYM);
    expect(
      (await upload(file, `kind=breakpad&version=${VERSION}`, TOKEN, unconfigured)).statusCode,
    ).toBe(503);
    expect((await upload(file, `kind=breakpad&version=${VERSION}`, null)).statusCode).toBe(401);
    expect((await upload(file, `kind=breakpad&version=${VERSION}`, 'wrong')).statusCode).toBe(401);
    expect((await upload(file, 'kind=breakpad&version=0.11.8-dev')).statusCode).toBe(400);
    expect(
      (await upload(Buffer.from(MAC_SYM), `kind=breakpad&version=${VERSION}`)).statusCode,
    ).toBe(400);
    expect(
      (await upload(gzipSync('INFO x\n'), `kind=breakpad&version=${VERSION}`)).statusCode,
    ).toBe(400);
    expect((await upload(file, `kind=pdb&version=${VERSION}`)).statusCode).toBe(400);
    // Truncated gzip fails its check rather than storing half a file.
    expect(
      (await upload(file.subarray(0, file.length - 8), `kind=breakpad&version=${VERSION}`))
        .statusCode,
    ).toBe(400);
  });

  it('stores Breakpad symbols and wasm symbol maps, replacing a re-upload', async () => {
    const first = await upload(gzipSync(MAC_SYM), `kind=breakpad&version=${VERSION}`);
    expect(first.statusCode).toBe(201);
    expect(first.json<CrashSymbolReceipt>()).toEqual({
      kind: 'breakpad',
      module: 'glob2',
      debugId: '70B89F2716343580A69557CDB41D77430',
      version: VERSION,
      replaced: false,
    });
    const before = await harness.database.db
      .selectFrom('crash_symbols')
      .select('blob_key')
      .where('module', '=', 'glob2')
      .executeTakeFirstOrThrow();
    const again = await upload(gzipSync(MAC_SYM), `kind=breakpad&version=${VERSION}`);
    expect(again.statusCode).toBe(200);
    expect(again.json<CrashSymbolReceipt>().replaced).toBe(true);
    expect(await harness.blobs.size(before.blob_key)).toBeUndefined();

    const map = gzipSync('0:abort\n88:Engine::run()\n4127:Unit::step(int)\n');
    expect((await upload(map, `kind=wasm&version=${VERSION}`)).statusCode).toBe(400);
    expect((await upload(map, `kind=wasm&version=${VERSION}&buildId=${BUILD_ID}`)).statusCode).toBe(
      201,
    );
    expect(
      await harness.database.db
        .selectFrom('crash_releases')
        .select('version')
        .where('version', '=', VERSION)
        .executeTakeFirst(),
    ).toBeTruthy();
  });
});

describe('crash reports', () => {
  it('needs the crash entry and a released version', async () => {
    expect((await report('metrickit', [['log.txt', 'x']])).statusCode).toBe(400);
    expect(
      (await report('metrickit', [['metrickit.json', metrickit([0x1010])]], { version: '0.11.7' }))
        .statusCode,
    ).toBe(403);
  });

  it('queues a job, symbolicates and groups MetricKit crashes across reports', async () => {
    const one = await report('metrickit', [['metrickit.json', metrickit([0x1010, 0x2010])]]);
    expect(one.statusCode).toBe(201);
    const { id } = one.json<{ id: string }>();
    const jobs = await sql<{ count: number }>`SELECT count(*)::integer AS count
      FROM graphile_worker.jobs WHERE key = ${`crash-${id}`}`.execute(harness.database.db);
    expect(jobs.rows[0]?.count).toBe(1);
    const pending = await harness.database.db
      .selectFrom('crash_reports')
      .select('status')
      .where('id', '=', id)
      .executeTakeFirstOrThrow();
    expect(pending.status).toBe('pending');

    const row = await processReport(id);
    expect(row).toMatchObject({ status: 'done', reason: 'EXC_BAD_ACCESS / SIGSEGV' });
    expect(row.frames?.map((f) => [f.function, f.line])).toEqual([
      ['Unit::step(int)', 412],
      ['Team::update()', 90],
    ]);
    // Same top frames from another install and channel land in the same group.
    const two = await report('metrickit', [['metrickit.json', metrickit([0x1020, 0x2030])]], {
      distribution: 'direct',
      match: { tick: 900, seed: 77 },
    });
    const other = await processReport(two.json<{ id: string }>().id);
    expect(other.group_id).toBe(row.group_id);
    expect(other.match_tick).toBe(900);
    // Processing again is a no-op.
    expect((await processReport(id)).processed_at).toEqual(row.processed_at);

    const groups = await admin<AdminCrashGroupList>('/api/v1/admin/crash-groups');
    expect(groups.status).toBe(200);
    const group = groups.body.items.find((g) => g.id === row.group_id);
    expect(group).toMatchObject({
      signature: 'Unit::step(int) | Team::update()',
      firstVersion: VERSION,
      lastVersion: VERSION,
      reports: 2,
      installs: 2,
      platforms: ['ios'],
      distributions: ['app_store', 'direct'],
    });
    const filtered = await admin<AdminCrashGroupList>(
      '/api/v1/admin/crash-groups?distribution=direct',
    );
    expect(filtered.body.items.find((g) => g.id === row.group_id)?.reports).toBe(1);
    const none = await admin<AdminCrashGroupList>('/api/v1/admin/crash-groups?version=9.9.9');
    expect(none.body.items).toEqual([]);

    const inGroup = await admin<AdminCrashReportList>(
      `/api/v1/admin/crash-reports?group=${row.group_id}`,
    );
    expect(inGroup.body.items.map((r) => r.id).sort()).toEqual([id, other.id].sort());
    expect(inGroup.body.items[0]).toMatchObject({
      kind: 'metrickit',
      status: 'done',
      groupId: row.group_id,
    });
    const detail = await admin<AdminCrashReportDetail>(`/api/v1/admin/crash-reports/${id}`);
    expect(detail.body.frames[0]).toMatchObject({ function: 'Unit::step(int)', inGame: true });
    expect((await admin(`/api/v1/admin/crash-reports?group=nope`)).status).toBe(400);
    expect((await admin(`/api/v1/admin/crash-reports?kind=nope`)).status).toBe(400);
    const problems = await admin<AdminCrashReportList>('/api/v1/admin/crash-reports?kind=problem');
    expect(problems.body.items.every((r) => r.kind === 'problem')).toBe(true);
    expect((await admin(`/api/v1/admin/crash-reports/ZZZZZZZZ`)).status).toBe(404);
  });

  it('names browser frames from the build symbol map', async () => {
    const sent = await report(
      'web',
      [
        [
          'web-error.json',
          JSON.stringify({
            schema: 1,
            source: 'abort',
            message: 'RuntimeError: memory access out of bounds',
            stack: 'at wasm-function[4127]:0x1a2b\nat wasm-function[88]:0x4f00',
            buildId: BUILD_ID,
          }),
        ],
      ],
      { platform: 'web', distribution: 'browser' },
    );
    const row = await processReport(sent.json<{ id: string }>().id);
    expect(row.status).toBe('done');
    expect(row.frames?.map((f) => f.function)).toEqual(['Unit::step(int)', 'Engine::run()']);
  });

  it('reads uncaught Java exceptions from Android', async () => {
    const sent = await report(
      'java',
      [
        [
          'java-exception.txt',
          [
            'thread: main',
            'java.lang.NullPointerException: no surface',
            '\tat org.globulation2.glob2.Glob2Activity.setPreparingAssets(Glob2Activity.java:41)',
            '\tat android.os.Handler.handleCallback(Handler.java:942)',
          ].join('\n'),
        ],
      ],
      { platform: 'android', distribution: 'google_play' },
    );
    expect(sent.statusCode).toBe(201);
    const row = await processReport(sent.json<{ id: string }>().id);
    expect(row.status).toBe('done');
    expect(row.reason).toBe('java.lang.NullPointerException: no surface');
    expect(row.frames?.[0]).toMatchObject({ module: 'java', line: 41, inGame: true });
  });

  it('groups non-fatal errors by category and message shape', async () => {
    const send = (message: string) =>
      report('error', [
        ['error.json', JSON.stringify({ schema: 1, category: 'map-load', message })],
        ['log.txt', 'Game initialization failed'],
      ]);
    const one = await send('unknown terrain type 41 at (12, 7)');
    const two = await send('unknown terrain type 3 at (0, 1)');
    expect([one.statusCode, two.statusCode]).toEqual([201, 201]);
    const first = await processReport(one.json<{ id: string }>().id);
    const second = await processReport(two.json<{ id: string }>().id);
    expect(first.status).toBe('done');
    expect(first.reason).toBe('map-load: unknown terrain type 41 at (12, 7)');
    expect(first.group_id).toBe(second.group_id);
  });

  it('marks a crash that cannot be read as failed instead of retrying', async () => {
    const sent = await report('web', [['web-error.json', '{"schema":1}']]);
    const row = await processReport(sent.json<{ id: string }>().id);
    expect(row.status).toBe('failed');
    expect(row.reason).toMatch(/WebErrorReport/);
    expect(row.group_id).toBeNull();
  });

  it.skipIf(!STACKWALK)('symbolicates a minidump with minidump-stackwalk', async () => {
    const sym = readFileSync(join(FIXTURES, 'test_app.sym'));
    expect((await upload(gzipSync(sym), `kind=breakpad&version=${VERSION}`)).statusCode).toBe(201);
    const sent = await report(
      'crash',
      [['minidump.dmp', readFileSync(join(FIXTURES, 'test.dmp'))]],
      {
        platform: 'windows',
        distribution: 'steam',
      },
    );
    expect(sent.statusCode).toBe(201);
    const row = await processReport(sent.json<{ id: string }>().id);
    expect(row.status).toBe('done');
    expect(row.reason).toBe('EXCEPTION_ACCESS_VIOLATION_WRITE');
    expect(row.frames?.slice(0, 3).map((f) => [f.module, f.function, f.line])).toEqual([
      ['test_app.exe', "`anonymous namespace'::CrashFunction", 58],
      ['test_app.exe', 'main', 65],
      ['test_app.exe', '__tmainCRTStartup', 327],
    ]);
  });

  it('sweeps symbol uploads no row names', async () => {
    const key = 'crash-symbols/breakpad/glob2/orphan.sym.gz';
    await harness.blobs.put(key, gzipSync('MODULE x'));
    const result = await retainCrashReports(harness.database.db, harness.blobs, {
      now: Date.now() + 2 * 86_400_000,
    });
    expect(result.deletedOrphanSymbols).toBeGreaterThanOrEqual(1);
    expect(await harness.blobs.size(key)).toBeUndefined();
    // Stored symbols stay.
    const kept = await harness.database.db.selectFrom('crash_symbols').select('blob_key').execute();
    for (const row of kept) expect(await harness.blobs.size(row.blob_key)).toBeGreaterThan(0);
  });
});
