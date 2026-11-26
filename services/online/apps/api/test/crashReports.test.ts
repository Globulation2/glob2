import { deflateRawSync, crc32 } from 'node:zlib';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { schemaIssues, CrashReportContext } from '@glob2/protocol';
import { createHarness, type Harness, type Instance } from './support.ts';
import { seedHistory, SEEDED_QUEUES, type SeededHistory } from './historySeed.ts';
import { MemoryLimit, CRASH_REPORTS_PER_INSTALL_DAY } from '../src/crashReports/routes.ts';
import { listEntries, ZipError } from '../src/crashReports/zip.ts';
import { retainCrashReports } from '../../worker/src/crashReportRetention.ts';

let harness: Harness;
let api: Instance;
let seed: SeededHistory;
beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({ origin: 'http://play.test', instance: { queues: SEEDED_QUEUES } });
  seed = await seedHistory(harness.database.db, harness.blobs);
});
afterAll(async () => harness?.close());

/** A zip of stored entries, as the game writes it (optionally one deflated). */
function zip(entries: [string, Buffer | string][], deflate = new Set<string>()): Buffer {
  const locals: Buffer[] = [];
  const central: Buffer[] = [];
  let offset = 0;
  for (const [name, value] of entries) {
    const data = Buffer.from(value);
    const packed = deflate.has(name) ? deflateRawSync(data) : data;
    const method = deflate.has(name) ? 8 : 0;
    const nameBytes = Buffer.from(name);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(method, 8);
    local.writeUInt32LE(crc32(data), 14);
    local.writeUInt32LE(packed.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBytes.length, 26);
    const head = Buffer.alloc(46);
    head.writeUInt32LE(0x02014b50, 0);
    head.writeUInt16LE(20, 4);
    head.writeUInt16LE(20, 6);
    head.writeUInt16LE(method, 10);
    head.writeUInt32LE(crc32(data), 16);
    head.writeUInt32LE(packed.length, 20);
    head.writeUInt32LE(data.length, 24);
    head.writeUInt16LE(nameBytes.length, 28);
    head.writeUInt32LE(offset, 42);
    locals.push(local, nameBytes, packed);
    central.push(head, nameBytes);
    offset += 30 + nameBytes.length + packed.length;
  }
  const directory = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(directory.length, 12);
  end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, directory, end]);
}

const INSTALL = '3d2c1b0a-9f8e-4d7c-8b6a-5f4e3d2c1b0a';
function context(overrides: Record<string, unknown> = {}) {
  return {
    schema: 1,
    kind: 'problem',
    installId: INSTALL,
    version: '0.9.6',
    versionMinor: 219,
    simRevision: 170,
    distribution: 'direct',
    platform: 'linux',
    os: 'Linux',
    deviceClass: 'desktop',
    deviceTier: 'high',
    description: 'Units froze after loading my save.',
    createdAt: '2026-11-26T12:00:00Z',
    ...overrides,
  };
}
function bundle(overrides: Record<string, unknown> = {}, extra: [string, string][] = []) {
  return zip([
    ['context.json', JSON.stringify(context(overrides))],
    ['log.txt', 'Glob2 screen ready: MainMenuScreen\n'],
    ...extra,
  ]);
}
function send(payload: Buffer, ip = '203.0.113.7', type = 'application/zip') {
  return api.app.inject({
    method: 'POST',
    url: '/api/v1/crash-reports',
    headers: { 'content-type': type },
    remoteAddress: ip,
    payload,
  });
}
function admin(url: string, session = seed.adminSession) {
  return api.app.inject({
    method: 'GET',
    url,
    headers: { cookie: `glob2_session=${session}`, origin: 'http://play.test' },
  });
}

describe('report bundles', () => {
  it('lists stored and deflated entries and refuses malformed archives', () => {
    const good = zip(
      [
        ['context.json', '{}'],
        ['log.txt', 'x'.repeat(1000)],
      ],
      new Set(['log.txt']),
    );
    expect(listEntries(good, 1 << 20).map((e) => [e.name, e.size])).toEqual([
      ['context.json', 2],
      ['log.txt', 1000],
    ]);
    expect(() => listEntries(Buffer.from('not a zip at all, really not'), 1 << 20)).toThrow(
      ZipError,
    );
    expect(() => listEntries(zip([['../evil', 'x']]), 1 << 20)).toThrow(ZipError);
    expect(() =>
      listEntries(
        zip([
          ['a.txt', 'x'],
          ['a.txt', 'y'],
        ]),
        1 << 20,
      ),
    ).toThrow(ZipError);
    // Declared unpacked size counts against the limit before anything is inflated.
    expect(() => listEntries(good, 500)).toThrow(/too large/);
  });

  it('matches the CrashReportContext contract the game writes', () => {
    expect(schemaIssues(CrashReportContext, context())).toEqual([]);
    expect(schemaIssues(CrashReportContext, context({ accountId: 'x' }))).not.toEqual([]);
  });
});

describe('POST /api/v1/crash-reports', () => {
  it('stores a bundle anonymously and answers with a short ID', async () => {
    const response = await send(bundle({}, [['save.game', 'GLO2save']]));
    expect(response.statusCode).toBe(201);
    expect(response.headers['access-control-allow-origin']).toBe('*');
    const { id } = response.json<{ id: string }>();
    expect(id).toMatch(/^[A-HJ-NP-Z2-9]{8}$/);
    const row = await harness.database.db
      .selectFrom('crash_reports')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirstOrThrow();
    expect(row).toMatchObject({
      kind: 'problem',
      install_id: INSTALL,
      version_minor: 219,
      sim_revision: 170,
      platform: 'linux',
      description: 'Units froze after loading my save.',
      attachments: [
        { name: 'log.txt', size: 35 },
        { name: 'save.game', size: 8 },
      ],
    });
    expect(await harness.blobs.size(row.bundle_key!)).toBe(row.size);
  });

  it('answers the browser preflight', async () => {
    const response = await api.app.inject({
      method: 'OPTIONS',
      url: '/api/v1/crash-reports',
      headers: {
        origin: 'https://html.itch.zone',
        'access-control-request-method': 'POST',
        'access-control-request-headers': 'content-type',
      },
    });
    expect(response.statusCode).toBe(204);
    expect(response.headers['access-control-allow-origin']).toBe('*');
    expect(response.headers['access-control-allow-headers']).toBe('content-type');
  });

  it('refuses bundles that are not zips, lack a valid context or carry extra fields', async () => {
    expect((await send(Buffer.from('hello'), '203.0.113.8')).statusCode).toBe(400);
    expect((await send(zip([['log.txt', 'x']]), '203.0.113.8')).statusCode).toBe(400);
    expect((await send(zip([['context.json', '{not json']]), '203.0.113.8')).statusCode).toBe(400);
    expect((await send(bundle({ accountName: 'Alice' }), '203.0.113.8')).statusCode).toBe(400);
    expect((await send(bundle({ description: 'x'.repeat(2001) }), '203.0.113.8')).statusCode).toBe(
      400,
    );
    expect((await send(bundle(), '203.0.113.8', 'application/json')).statusCode).toBe(400);
  });

  it('limits reports per install and day', async () => {
    const installId = '0f0e0d0c-0b0a-4908-8706-050403020100';
    for (let i = 0; i < CRASH_REPORTS_PER_INSTALL_DAY; i++)
      expect((await send(bundle({ installId }), `198.51.100.${i}`)).statusCode).toBe(201);
    const refused = await send(bundle({ installId }), '198.51.100.250');
    expect(refused.statusCode).toBe(429);
    expect(refused.headers['retry-after']).toBeDefined();
  });

  it('keeps per-address limits in memory only', () => {
    let now = 0;
    const limit = new MemoryLimit(2, 1000, () => now);
    expect([limit.take('a'), limit.take('a'), limit.take('a'), limit.take('b')]).toEqual([
      true,
      true,
      false,
      true,
    ]);
    now = 1000;
    expect(limit.take('a')).toBe(true);
  });
});

describe('admin crash reports', () => {
  it('lists reports and serves bundles and attachments to administrators only', async () => {
    const { id } = (await send(bundle({ description: 'listed' }), '192.0.2.1')).json<{
      id: string;
    }>();
    expect((await admin('/api/v1/admin/crash-reports', seed.userSession)).statusCode).toBe(403);
    const list = await admin('/api/v1/admin/crash-reports?limit=1');
    expect(list.statusCode).toBe(200);
    const page = list.json<{ items: { id: string }[]; nextCursor?: string }>();
    expect(page.items[0]).toMatchObject({ id, description: 'listed', bundleAvailable: true });
    expect(page.nextCursor).toBeDefined();
    const next = await admin(`/api/v1/admin/crash-reports?limit=1&cursor=${page.nextCursor}`);
    expect(next.json<{ items: { id: string }[] }>().items[0]!.id).not.toBe(id);

    const whole = await admin(`/api/v1/admin/crash-reports/${id}/bundle`);
    expect(whole.statusCode).toBe(200);
    expect(whole.headers['content-type']).toBe('application/zip');
    expect(listEntries(whole.rawPayload, 1 << 20).map((e) => e.name)).toEqual([
      'context.json',
      'log.txt',
    ]);
    const log = await admin(`/api/v1/admin/crash-reports/${id}/attachments/log.txt`);
    expect(log.statusCode).toBe(200);
    expect(log.body).toContain('screen ready');
    expect(log.headers['content-disposition']).toMatch(/^attachment/);
    expect((await admin(`/api/v1/admin/crash-reports/${id}/attachments/nope.txt`)).statusCode).toBe(
      404,
    );
    const audit = await harness.database.db
      .selectFrom('admin_audit_log')
      .select(['action', 'target_id'])
      .where('target_type', '=', 'crash_report')
      .where('target_id', '=', id)
      .execute();
    expect(audit).toHaveLength(2);
  });

  it('drops bundles after 90 days and summaries after two years', async () => {
    const db = harness.database.db;
    const old = (await send(bundle({ description: 'old' }), '192.0.2.2')).json<{ id: string }>();
    const ancient = (await send(bundle({ description: 'ancient' }), '192.0.2.2')).json<{
      id: string;
    }>();
    await db
      .updateTable('crash_reports')
      .set({ received_at: new Date(Date.now() - 100 * 86_400_000) })
      .where('id', '=', old.id)
      .execute();
    await db
      .updateTable('crash_reports')
      .set({ received_at: new Date(Date.now() - 800 * 86_400_000) })
      .where('id', '=', ancient.id)
      .execute();
    const key = (
      await db
        .selectFrom('crash_reports')
        .select('bundle_key')
        .where('id', '=', old.id)
        .executeTakeFirstOrThrow()
    ).bundle_key!;
    const first = await retainCrashReports(harness.database.as('worker').db, harness.blobs);
    expect(first).toMatchObject({ deletedBundles: 2, deletedReports: 1 });
    expect(await harness.blobs.size(key)).toBeUndefined();
    const left = await db
      .selectFrom('crash_reports')
      .select(['id', 'bundle_key'])
      .where('id', 'in', [old.id, ancient.id])
      .execute();
    expect(left).toEqual([{ id: old.id, bundle_key: null }]);
    expect((await admin(`/api/v1/admin/crash-reports/${old.id}/bundle`)).statusCode).toBe(404);
  });
});
