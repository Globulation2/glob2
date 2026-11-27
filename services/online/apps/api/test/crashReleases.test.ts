// Release gates for crash reports (docs/multiplayer/crash-reports.md#release-gates):
// session counts, the per-release admin view, the promotion gate, triage
// with automatic reopening, and GitHub issues for new crash groups.
import { randomUUID } from 'node:crypto';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type {
  AdminCrashGroupList,
  AdminCrashReleaseList,
  CrashGroupStateReceipt,
  CrashReleaseGate,
  CrashSessionPing,
} from '@glob2/protocol';
import { createLogger } from '@glob2/core';
import { createHarness, type Harness, type Instance } from './support.ts';
import { seedHistory, SEEDED_QUEUES, type SeededHistory } from './historySeed.ts';
import { zip } from './zipBundle.ts';
import { newCrashReportId } from '../src/crashReports/routes.ts';
import { acceptedDay, recordSessions } from '../src/crashReports/releases.ts';
import { processCrashReport } from '../../crash-worker/src/process.ts';
import { SymbolCache } from '../../crash-worker/src/symbols.ts';
import { issueContent, openCrashIssue } from '../../crash-worker/src/githubIssue.ts';
import { retainCrashReports } from '../../worker/src/crashReportRetention.ts';

const TOKEN = 'release-workflow-token';
const PREVIOUS = '0.20.0.1';
const CURRENT = '0.20.0.2';
const LATER = '0.20.1';
const logger = createLogger('crash-release-test', 'silent');
const today = new Date().toISOString().slice(0, 10);

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
  await harness.database.db
    .insertInto('crash_releases')
    .values([PREVIOUS, CURRENT, LATER].map((version) => ({ version })))
    .execute();
});
afterAll(async () => harness?.close());

const db = () => harness.database.db;

function ping(overrides: Partial<CrashSessionPing> = {}): CrashSessionPing {
  return {
    schema: 1,
    installId: randomUUID(),
    version: CURRENT,
    versionMinor: 219,
    distribution: 'google_play',
    platform: 'android',
    day: today,
    sessions: 1,
    ...overrides,
  };
}
function send(body: unknown, instance: Instance = api) {
  return instance.app.inject({
    method: 'POST',
    url: '/api/v1/crash-sessions',
    headers: { 'content-type': 'application/json' },
    remoteAddress: `203.0.113.${Math.floor(Math.random() * 200)}`,
    payload: JSON.stringify(body),
  });
}
function admin<T>(url: string, method: 'GET' | 'POST' = 'GET', payload?: unknown) {
  return api.app
    .inject({
      method,
      url,
      headers: {
        cookie: `glob2_session=${seed.adminSession}`,
        origin: 'http://play.test',
        ...(payload ? { 'content-type': 'application/json' } : {}),
      },
      ...(payload ? { payload: JSON.stringify(payload) } : {}),
    })
    .then((r) => ({ status: r.statusCode, body: r.json<T>() }));
}
function gate(version: string, token: string | null = TOKEN, instance: Instance = api) {
  return instance.app.inject({
    method: 'GET',
    url: `/api/v1/crash-releases/${version}/gate`,
    headers: token ? { authorization: `Bearer ${token}` } : {},
  });
}
async function group(signature: string, firstVersion: string) {
  return (
    await db()
      .insertInto('crash_groups')
      .values({ signature, first_version: firstVersion, last_version: firstVersion })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
}
/** A processed crash report, as the crash worker leaves it. */
async function crash(version: string, installId: string, groupId: string, platform = 'android') {
  await db()
    .insertInto('crash_reports')
    .values({
      id: newCrashReportId(),
      submission_id: randomUUID(),
      kind: 'java',
      install_id: installId,
      version,
      version_minor: 219,
      sim_revision: 170,
      distribution: platform === 'android' ? 'google_play' : 'direct',
      platform,
      os: 'Android 15',
      device_class: 'phone',
      device_tier: 'high',
      description: 'my secret description',
      size: 10,
      status: 'done',
      group_id: groupId,
      frames: JSON.stringify([
        {
          module: 'java',
          offset: '0x0',
          function: 'org.globulation2.glob2.Map.load',
          file: '/home/runner/work/glob2/glob2/src/map/Map.java',
          line: 7,
          inGame: true,
        },
      ]),
    })
    .execute();
}
/** `installs` opted-in installs reporting `sessions` sessions in all. */
async function sessions(version: string, installs: string[], total: number, platform = 'android') {
  for (const [index, installId] of installs.entries()) {
    const share = Math.floor(total / installs.length) + (index < total % installs.length ? 1 : 0);
    await recordSessions(
      db(),
      ping({
        installId,
        version,
        sessions: share,
        platform: platform as CrashSessionPing['platform'],
        distribution: platform === 'android' ? 'google_play' : 'direct',
      }),
    );
  }
}
const installs = (n: number) => Array.from({ length: n }, () => randomUUID());

describe('session counts', () => {
  it('accepts days from the last week only', () => {
    const now = Date.parse('2026-11-27T12:00:00Z');
    expect(acceptedDay('2026-11-27', now)).toBe(true);
    expect(acceptedDay('2026-11-28', now)).toBe(true);
    expect(acceptedDay('2026-11-21', now)).toBe(true);
    expect(acceptedDay('2026-11-20', now)).toBe(false);
    expect(acceptedDay('2026-11-29', now)).toBe(false);
    expect(acceptedDay('2026-02-30', now)).toBe(false);
  });

  it('counts running day totals, so a resent ping changes nothing', async () => {
    const installId = randomUUID();
    const body = ping({ installId, version: LATER, platform: 'linux', distribution: 'direct' });
    const first = await send({ ...body, sessions: 2 });
    expect(first.statusCode).toBe(204);
    expect(first.headers['access-control-allow-origin']).toBe('*');
    expect((await send({ ...body, sessions: 2 })).statusCode).toBe(204);
    expect((await send({ ...body, sessions: 5 })).statusCode).toBe(204);
    // An older total arriving late does not lower it.
    expect((await send({ ...body, sessions: 3 })).statusCode).toBe(204);
    await send(ping({ version: LATER, platform: 'linux', distribution: 'direct', sessions: 4 }));
    const day = await db()
      .selectFrom('crash_session_days')
      .select(['sessions', 'installs'])
      .where('version', '=', LATER)
      .executeTakeFirstOrThrow();
    expect({ sessions: Number(day.sessions), installs: day.installs }).toEqual({
      sessions: 9,
      installs: 2,
    });
    const row = await db()
      .selectFrom('crash_session_installs')
      .select('sessions')
      .where('install_id', '=', installId)
      .executeTakeFirstOrThrow();
    expect(row.sessions).toBe(5);
  });

  it('refuses unreleased versions, old days and malformed pings', async () => {
    expect((await send(ping({ version: '0.9.9.9' }))).statusCode).toBe(403);
    expect((await send(ping({ day: '2020-01-01' }))).statusCode).toBe(400);
    expect((await send({ ...ping(), installId: 'nope' })).statusCode).toBe(400);
    expect((await send({ ...ping(), address: '198.51.100.1' })).statusCode).toBe(400);
    const preflight = await api.app.inject({ method: 'OPTIONS', url: '/api/v1/crash-sessions' });
    expect(preflight.statusCode).toBe(204);
    expect(preflight.headers['access-control-allow-headers']).toBe('content-type');
  });
});

describe('release view and gate', () => {
  it('compares crash-free sessions with the previous release and needs triage', async () => {
    expect((await gate(CURRENT, TOKEN, unconfigured)).statusCode).toBe(503);
    expect((await gate(CURRENT, null)).statusCode).toBe(401);
    expect((await gate(CURRENT, 'wrong')).statusCode).toBe(401);
    expect((await gate('0.9.9.9')).statusCode).toBe(404);
    expect((await gate('latest')).statusCode).toBe(400);

    // The previous release: 2000 sessions from 100 installs, 10 crashes from 8 of them.
    const previous = installs(100);
    await sessions(PREVIOUS, previous, 2000);
    const old = await group('Map::save()', PREVIOUS);
    for (let i = 0; i < 10; i++) await crash(PREVIOUS, previous[i % 8]!, old);
    // A crash from an install that never opted in counts as a report, not against the rate.
    await crash(PREVIOUS, randomUUID(), old);

    const current = installs(40);
    await sessions(CURRENT, current, 400);
    const early = (await gate(CURRENT)).json<CrashReleaseGate>();
    expect(early).toMatchObject({
      verdict: 'insufficient_data',
      previousVersion: PREVIOUS,
      sessions: 400,
      previousSessions: 2000,
      previousCrashFreeSessions: 0.995,
      minimumSessions: 1000,
      tolerance: 0.005,
    });

    // 2000 sessions with 15 crashes (99.25%) is within 0.5 points of 99.5%,
    // but the new group behind them is untriaged.
    await sessions(CURRENT, installs(60), 1600, 'linux');
    const fresh = await group('Map::load()', CURRENT);
    for (let i = 0; i < 15; i++) await crash(CURRENT, current[i % 12]!, fresh);
    const blocked = (await gate(CURRENT)).json<CrashReleaseGate>();
    expect(blocked.verdict).toBe('fail');
    expect(blocked.crashFreeSessions).toBeCloseTo(0.9925);
    expect(blocked.untriagedGroups).toMatchObject([
      { id: fresh, signature: 'Map::load()', installs: 12 },
    ]);
    expect(blocked.reasons[0]).toMatch(/Untriaged new crash group.*Map::load\(\)/);

    const triaged = await admin<CrashGroupStateReceipt>(
      `/api/v1/admin/crash-groups/${fresh}/state`,
      'POST',
      { state: 'triaged' },
    );
    expect(triaged).toMatchObject({ status: 200, body: { id: fresh, state: 'triaged' } });
    const passed = (await gate(CURRENT)).json<CrashReleaseGate>();
    expect(passed.verdict).toBe('pass');
    expect(passed.reasons.join(' ')).toMatch(/99\.25% against 99\.50% in 0\.20\.0\.1/);

    // Ten more crashes (98.75%) fall more than half a point below.
    for (let i = 0; i < 10; i++) await crash(CURRENT, current[12 + i]!, fresh);
    const worse = (await gate(CURRENT)).json<CrashReleaseGate>();
    expect(worse.verdict).toBe('fail');
    expect(worse.reasons[0]).toMatch(/more than 0\.50% below 0\.20\.0\.1/);

    const view = await admin<AdminCrashReleaseList>('/api/v1/admin/crash-releases');
    expect(view.status).toBe(200);
    expect(view.body.items.map((r) => r.version)).toEqual([LATER, CURRENT, PREVIOUS]);
    const release = view.body.items[1]!;
    expect(release.overall).toMatchObject({
      sessions: 2000,
      installs: 100,
      crashedSessions: 25,
      crashedInstalls: 22,
      crashFreeSessions: 0.9875,
      crashFreeInstalls: 0.78,
    });
    expect(
      release.breakdown.map((b) => [b.platform, b.stats.sessions, b.stats.crashedSessions]),
    ).toEqual([
      ['linux', 1600, 0],
      ['android', 400, 25],
    ]);
    expect(release).toMatchObject({ reports: 25, affectedInstalls: 22 });
    expect(release.newGroups).toMatchObject([
      { id: fresh, state: 'triaged', reports: 25, installs: 22 },
    ]);
    const before = view.body.items[2]!;
    expect(before).toMatchObject({ reports: 11, affectedInstalls: 9 });
    expect(before.overall.crashedSessions).toBe(10);

    const page = await admin<AdminCrashReleaseList>('/api/v1/admin/crash-releases?limit=2');
    expect(page.body.items).toHaveLength(2);
    expect(page.body.nextCursor).toBe('o2');
  });
});

describe('triage', () => {
  function javaReport(version: string, method: string) {
    const context = {
      schema: 1,
      kind: 'java',
      installId: randomUUID(),
      submissionId: randomUUID(),
      version,
      versionMinor: 219,
      simRevision: 170,
      distribution: 'google_play',
      platform: 'android',
      os: 'Android 15',
      deviceClass: 'phone',
      deviceTier: 'high',
      description: 'it closed while I was playing with my friend',
      createdAt: '2026-11-26T12:00:00Z',
    };
    const trace = [
      'thread: main',
      'java.lang.IllegalStateException: boom',
      `\tat org.globulation2.glob2.Glob2Activity.${method}(Glob2Activity.java:41)`,
    ].join('\n');
    return api.app.inject({
      method: 'POST',
      url: '/api/v1/crash-reports',
      headers: { 'content-type': 'application/zip' },
      remoteAddress: `198.51.100.${Math.floor(Math.random() * 200)}`,
      payload: zip([
        ['context.json', JSON.stringify(context)],
        ['java-exception.txt', trace],
      ]),
    });
  }
  async function processed(version: string, method: string, issues = false) {
    const sent = await javaReport(version, method);
    expect(sent.statusCode).toBe(201);
    const { id } = sent.json<{ id: string }>();
    await processCrashReport(
      db(),
      harness.blobs,
      { stackwalk: { binary: '/nonexistent' }, cache, issues },
      id,
      logger,
    );
    return db()
      .selectFrom('crash_reports')
      .innerJoin('crash_groups', 'crash_groups.id', 'crash_reports.group_id')
      .select(['crash_groups.id', 'state', 'fixed_in', 'install_id'])
      .where('crash_reports.id', '=', id)
      .executeTakeFirstOrThrow();
  }

  it('validates and audits state changes', async () => {
    const id = await group('Unit::step(int)', CURRENT);
    const url = `/api/v1/admin/crash-groups/${id}/state`;
    expect((await admin(url, 'POST', { state: 'resolved' })).status).toBe(400);
    expect((await admin(url, 'POST', { state: 'triaged', fixedIn: LATER })).status).toBe(400);
    expect((await admin(url, 'POST', { state: 'gone' })).status).toBe(400);
    expect(
      (await admin(`/api/v1/admin/crash-groups/${randomUUID()}/state`, 'POST', { state: 'new' }))
        .status,
    ).toBe(404);
    const anonymous = await api.app.inject({
      method: 'POST',
      url,
      headers: { 'content-type': 'application/json', origin: 'http://play.test' },
      payload: JSON.stringify({ state: 'triaged' }),
    });
    expect(anonymous.statusCode).toBe(401);
    const resolved = await admin<CrashGroupStateReceipt>(url, 'POST', {
      state: 'resolved',
      fixedIn: LATER,
    });
    expect(resolved.body).toEqual({ id, state: 'resolved', fixedIn: LATER });
    const audit = await db()
      .selectFrom('admin_audit_log')
      .select(['action', 'details'])
      .where('target_id', '=', id)
      .executeTakeFirstOrThrow();
    expect(audit).toMatchObject({
      action: 'crash_group.state',
      details: { from: 'new', to: 'resolved', fixedIn: LATER },
    });
    const listed = await admin<AdminCrashGroupList>('/api/v1/admin/crash-groups?state=resolved');
    expect(listed.status).toBe(200);
    expect((await admin('/api/v1/admin/crash-groups?state=gone')).status).toBe(400);
  });

  it('reopens a resolved group that recurs in its fixed-in release or later', async () => {
    const first = await processed(CURRENT, 'reopenMe');
    expect(first.state).toBe('new');
    await admin(`/api/v1/admin/crash-groups/${first.id}/state`, 'POST', {
      state: 'resolved',
      fixedIn: LATER,
    });
    // Older builds still crash the old way: that is not a regression.
    expect(await processed(PREVIOUS, 'reopenMe')).toMatchObject({
      id: first.id,
      state: 'resolved',
    });
    expect(await processed(LATER, 'reopenMe')).toMatchObject({
      id: first.id,
      state: 'new',
      fixed_in: LATER,
    });
    const listed = await admin<AdminCrashGroupList>(`/api/v1/admin/crash-groups?version=${LATER}`);
    expect(listed.body.items.find((g) => g.id === first.id)).toMatchObject({
      state: 'new',
      fixedIn: LATER,
    });
  });

  it('queues one GitHub issue per new group and opens it without player data', async () => {
    const row = await processed(CURRENT, 'issueMe', true);
    await processed(CURRENT, 'issueMe', true);
    const jobs = await sql<{ count: number }>`SELECT count(*)::integer AS count
      FROM graphile_worker.jobs WHERE key = ${`crash-issue-${row.id}`}`.execute(db());
    expect(jobs.rows[0]?.count).toBe(1);
    expect((await processed(CURRENT, 'noIssue')).id).toBeTruthy();

    const requests: { url: string; init: RequestInit }[] = [];
    const fetchMock = (async (url: string | URL | Request, init?: RequestInit) => {
      requests.push({ url: String(url), init: init ?? {} });
      return new Response(
        JSON.stringify({ number: 77, html_url: 'https://github.com/o/r/issues/77' }),
        { status: 201 },
      );
    }) as typeof fetch;
    const config = {
      repository: 'Globulation2/glob2',
      token: 'gh-token',
      publicOrigin: 'http://play.test',
      fetch: fetchMock,
    };
    expect(await openCrashIssue(db(), config, row.id, logger)).toBe(77);
    expect(await openCrashIssue(db(), config, row.id, logger)).toBe(77);
    expect(requests).toHaveLength(1);
    const request = requests[0]!;
    expect(request.url).toBe('https://api.github.com/repos/Globulation2/glob2/issues');
    expect((request.init.headers as Record<string, string>)['authorization']).toBe(
      'Bearer gh-token',
    );
    const issue = JSON.parse(String(request.init.body)) as { title: string; body: string };
    expect(issue.title).toMatch(/^Crash: .*issueMe/);
    expect(issue.body).toContain(`http://play.test/admin/crashes?group=${row.id}`);
    expect(issue.body).toContain(`**First release:** ${CURRENT}`);
    expect(issue.body).toContain('| android | google_play | 2 | 2 |');
    expect(issue.body).not.toContain(row.install_id);
    expect(issue.body).not.toContain('my friend');
    const stored = await db()
      .selectFrom('crash_groups')
      .select(['github_issue', 'github_issue_url'])
      .where('id', '=', row.id)
      .executeTakeFirstOrThrow();
    expect(stored).toEqual({
      github_issue: 77,
      github_issue_url: 'https://github.com/o/r/issues/77',
    });

    const failing = (async () => new Response('', { status: 502 })) as unknown as typeof fetch;
    const other = await group('Other::crash()', CURRENT);
    await expect(
      openCrashIssue(db(), { ...config, fetch: failing }, other, logger),
    ).rejects.toThrow(/502/);
  });

  it('writes frames with repository paths and safe code fences', () => {
    const content = issueContent({
      groupId: 'g',
      signature: "`anonymous namespace'::f",
      firstVersion: CURRENT,
      frames: [
        {
          module: 'glob2',
          offset: '0x1',
          function: 'f',
          file: '/Users/runner/work/glob2/glob2/src/unit/Unit.cpp',
          line: 3,
          inGame: true,
        },
        { module: 'libc.so', offset: '0x2', inGame: false },
      ],
      counts: [],
      publicOrigin: 'http://play.test',
    });
    expect(content.body).toContain('0 * f src/unit/Unit.cpp:3');
    expect(content.body).toContain('1   libc.so+0x2');
    expect(content.body).not.toContain('/Users/runner');
  });
});

describe('retention', () => {
  it('deletes session counts after two years', async () => {
    const installId = randomUUID();
    await db()
      .insertInto('crash_session_installs')
      .values({
        day: '2020-01-01',
        version: PREVIOUS,
        platform: 'linux',
        distribution: 'direct',
        install_id: installId,
        sessions: 3,
      })
      .execute();
    await db()
      .insertInto('crash_session_days')
      .values({
        day: '2020-01-01',
        version: PREVIOUS,
        platform: 'linux',
        distribution: 'direct',
        sessions: 3,
        installs: 1,
      })
      .execute();
    const result = await retainCrashReports(db(), harness.blobs);
    expect(result.deletedSessionRows).toBe(2);
    expect(
      await db()
        .selectFrom('crash_session_installs')
        .select('day')
        .where('install_id', '=', installId)
        .executeTakeFirst(),
    ).toBeUndefined();
    // Recent counts stay.
    expect(
      Number(
        (
          await db()
            .selectFrom('crash_session_days')
            .select(sql<string>`count(*)`.as('n'))
            .executeTakeFirstOrThrow()
        ).n,
      ),
    ).toBeGreaterThan(0);
  });
});
