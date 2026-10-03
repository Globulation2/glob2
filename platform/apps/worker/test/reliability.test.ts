// States that used to be terminal: verify jobs that fail, get lost or run out
// of attempts; operator re-verification; retention; blob garbage collection.
import { mkdtempSync, rmSync, utimesSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  FsBlobStore,
  JobQueue,
  contentKey,
  createLogger,
  prepareJobQueue,
  putContent,
} from '@glob2/core';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { collectBlobs } from '../src/blobGc.ts';
import { runMaintenance } from '../src/maintenance.ts';
import {
  ensureVerifyJob,
  findStaleEngineJobs,
  handleEngineJobResult,
  insertBlob,
  reverifyMatch,
  sweepStaleEngineJobs,
} from '@glob2/play';
import {
  HASH,
  createAccount,
  createMatch,
  createVerifyJob,
  resultPayload,
  verified,
} from '@glob2/play/testing';

const logger = createLogger('test', 'silent');
let database: TestDatabase;
let queue: JobQueue;

beforeAll(async () => {
  // Runs as platform-worker does; fixtures the worker cannot write go through `as('api')`.
  database = await createTestDatabase({ role: 'worker' });
  await prepareJobQueue(database.pool, logger);
  queue = await JobQueue.create(database.pool, logger);
});

afterAll(async () => {
  await queue?.close();
  await database?.drop();
});

async function endedMatchWithRecord(): Promise<string> {
  const a = await createAccount(database.db, 'Ann');
  const b = await createAccount(database.db, 'Bob');
  const matchId = await createMatch(database.db, [
    { side: 0, accountId: a },
    { side: 1, accountId: b },
  ]);
  await insertBlob(database.db, HASH, 10, 'application/octet-stream', 'private');
  await database.db
    .insertInto('match_artifacts')
    .values({ match_id: matchId, kind: 'record', blob_sha256: HASH })
    .onConflict((oc) => oc.columns(['match_id', 'kind']).doNothing())
    .execute();
  return matchId;
}

async function matchState(id: string) {
  return database.db
    .selectFrom('matches')
    .select(['verification', 'rating_status', 'rating_note'])
    .where('id', '=', id)
    .executeTakeFirstOrThrow();
}

/** The job as agents see it: queued, unreported, leasable once its lease (if any) ends. */
async function queueJob(jobId: string) {
  return database.db
    .selectFrom('engine_jobs')
    .select(['attempts', 'max_attempts'])
    .where('id', '=', jobId)
    .where('status', '=', 'queued')
    .where('reported_at', 'is', null)
    .executeTakeFirst();
}

describe('verify jobs', () => {
  it('submits one verify job per match, also for concurrent end-report retries', async () => {
    const matchId = await endedMatchWithRecord();
    const ids = await Promise.all(
      Array.from({ length: 6 }, () => ensureVerifyJob(database.db, matchId)),
    );
    expect(new Set(ids).size).toBe(1);
    const rows = await database.db
      .selectFrom('engine_jobs')
      .select(['id', 'match_id'])
      .where('match_id', '=', matchId)
      .execute();
    expect(rows).toEqual([{ id: ids[0], match_id: matchId }]);
    // Agents lease the row itself.
    expect(await queueJob(ids[0]!)).toMatchObject({ attempts: 0, max_attempts: 3 });
  });

  it('marks the match failed (and unrated) when the agent reports a failure', async () => {
    const matchId = await endedMatchWithRecord();
    const jobId = await createVerifyJob(database.db, matchId);
    const errors: unknown[] = [];
    await handleEngineJobResult(
      database.db,
      {
        jobId,
        kind: 'verify-match',
        ok: false,
        error: { code: 'internal', message: 'engine crashed' },
        agent: 'agent-test',
      },
      { logger: { error: (o: unknown) => errors.push(o) } as never },
    );
    expect(await matchState(matchId)).toEqual({
      verification: 'failed',
      rating_status: 'unchanged',
      rating_note: 'verification_failed',
    });
    expect(errors).toHaveLength(1);
  });

  it('gives up lost results and unserved jobs through the result path', async () => {
    const lostMatch = await endedMatchWithRecord();
    const unservedMatch = await endedMatchWithRecord();
    const busyMatch = await endedMatchWithRecord();
    // Reported an hour ago, but no result task carries the report any more.
    const lost = await createVerifyJob(database.db, lostMatch);
    await sql`UPDATE engine_jobs SET reported_at = now() - interval '1 hour' WHERE id = ${lost}`.execute(
      database.as('migrator').db,
    );
    // Queued for seven hours; no agent of its sim version exists.
    const unserved = (await ensureVerifyJob(database.db, unservedMatch))!;
    // Queued for an hour: agents may still come.
    const busy = (await ensureVerifyJob(database.db, busyMatch))!;
    await sql`UPDATE engine_jobs SET created_at = now() - interval '7 hours' WHERE id = ${unserved}`.execute(
      database.as('migrator').db,
    );
    await sql`UPDATE engine_jobs SET created_at = now() - interval '1 hour' WHERE id = ${busy}`.execute(
      database.as('migrator').db,
    );

    const stale = await findStaleEngineJobs(database.db);
    expect(stale.filter((j) => [lost, unserved, busy].includes(j.jobId))).toEqual(
      expect.arrayContaining([
        { jobId: lost, kind: 'verify-match', reason: 'lost' },
        { jobId: unserved, kind: 'verify-match', reason: 'unserved' },
      ]),
    );
    expect(stale.some((j) => j.jobId === busy)).toBe(false);

    await sweepStaleEngineJobs(database.db);
    expect((await matchState(lostMatch)).verification).toBe('failed');
    expect((await matchState(unservedMatch)).verification).toBe('failed');
    expect((await matchState(busyMatch)).verification).toBe('pending');
    expect(await queueJob(unserved)).toBeUndefined();
    expect(await queueJob(busy)).toBeDefined();
    expect(await findStaleEngineJobs(database.db)).toEqual([]);
  });

  it('does not give up a job whose result is waiting to be applied', async () => {
    const matchId = await endedMatchWithRecord();
    const jobId = (await ensureVerifyJob(database.db, matchId))!;
    // The agent reported: the result task is queued for the worker.
    await sql`UPDATE engine_jobs SET reported_at = now() - interval '1 hour' WHERE id = ${jobId}`.execute(
      database.as('migrator').db,
    );
    await queue.enqueue(
      'platform:engine-job-result',
      resultPayload(jobId, verified(['won', 'lost'])),
    );
    expect((await findStaleEngineJobs(database.db)).some((j) => j.jobId === jobId)).toBe(false);
  });

  it('re-runs verification of a failed match, and only when that makes sense', async () => {
    const matchId = await endedMatchWithRecord();
    const first = await createVerifyJob(database.db, matchId);
    await handleEngineJobResult(database.db, {
      jobId: first,
      kind: 'verify-match',
      ok: false,
      error: { code: 'internal', message: 'boom' },
      agent: 'agent-test',
    });
    const again = await reverifyMatch(database.db, matchId);
    expect(again).toMatchObject({ ok: true, previous: 'failed' });
    if (!again.ok) throw new Error('unreachable');
    expect(await matchState(matchId)).toEqual({
      verification: 'pending',
      rating_status: 'pending',
      rating_note: null,
    });
    expect(await queueJob(again.jobId)).toBeDefined();
    // A job is in flight now; only force replaces it.
    expect(await reverifyMatch(database.db, matchId)).toEqual({ ok: false, reason: 'in_progress' });
    const forced = await reverifyMatch(database.db, matchId, { force: true });
    expect(forced.ok).toBe(true);
    expect(await queueJob(again.jobId)).toBeUndefined();
    // The new verdict lands and ratings apply as usual.
    if (!forced.ok) throw new Error('unreachable');
    await handleEngineJobResult(
      database.db,
      resultPayload(forced.jobId, verified(['won', 'lost'])),
    );
    expect(await matchState(matchId)).toMatchObject({
      verification: 'verified',
      rating_status: 'applied',
    });
    expect(await reverifyMatch(database.db, matchId)).toEqual({
      ok: false,
      reason: 'already_verified',
    });
  });
});

describe('retention', () => {
  it('deletes what the policy says and keeps the rest', async () => {
    // Fixtures as the API writes them; retention runs as the worker.
    const db = database.as('api').db;
    const old = sql<Date>`now() - interval '200 days'`;
    const oldGuest = await createAccount(db, 'Old guest', 'guest');
    const playedGuest = await createAccount(db, 'Played guest', 'guest');
    const freshGuest = await createAccount(db, 'Fresh guest', 'guest');
    const reportingGuest = await createAccount(db, 'Reporting guest', 'guest');
    await insertBlob(db, 'ac'.repeat(32), 10, 'image/png', 'private');
    const reportedSkin = await db
      .insertInto('colony_skins')
      .values({ kind: 'preset', name: 'Reported paint', entitlement: 'skins:test' })
      .returning('id')
      .executeTakeFirstOrThrow();
    const reportedVersion = await db
      .insertInto('colony_skin_versions')
      .values({
        skin_id: reportedSkin.id,
        texture_sha256: 'ac'.repeat(32),
        layout: 'colony-v1',
        building_color: 123,
        manifest_sha256: 'ab'.repeat(32),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('colony_skin_reports')
      .values({
        version_id: reportedVersion.id,
        reporter_account_id: reportingGuest,
        reason: 'Review paint',
      })
      .execute();
    await db
      .updateTable('accounts')
      .set({ created_at: old })
      .where('id', 'in', [oldGuest, playedGuest, reportingGuest])
      .execute();
    await createMatch(db, [
      { side: 0, accountId: playedGuest },
      { side: 1, ai: 'nicowar' },
    ]);
    const registered = await createAccount(db, 'Member');
    await db
      .updateTable('accounts')
      .set({ created_at: old })
      .where('id', '=', registered)
      .execute();

    // Refresh tokens: rotated long ago (past reuse detection), revoked long ago, live.
    const family = crypto.randomUUID();
    await db
      .insertInto('refresh_tokens')
      .values([
        {
          account_id: registered,
          family_id: family,
          token_hash: 'a1'.repeat(32),
          expires_at: new Date(Date.now() + 86_400_000),
          rotated_at: new Date(Date.now() - 8 * 86_400_000),
        },
        {
          account_id: registered,
          family_id: family,
          token_hash: 'a2'.repeat(32),
          expires_at: new Date(Date.now() + 86_400_000),
          revoked_at: new Date(Date.now() - 8 * 86_400_000),
        },
        {
          account_id: registered,
          family_id: family,
          token_hash: 'a3'.repeat(32),
          expires_at: new Date(Date.now() + 86_400_000),
        },
      ])
      .execute();
    // A finished sign-in attempt from last month.
    await db
      .insertInto('signin_attempts')
      .values({
        confirmation_code: 'OLD999',
        status: 'completed',
        expires_at: new Date(Date.now() - 30 * 86_400_000),
        created_at: new Date(Date.now() - 30 * 86_400_000),
      })
      .execute();
    // Old chat in a room.
    const room = await db
      .insertInto('rooms')
      .values({
        code: 'RETENTIONX',
        name: 'Old',
        visibility: 'link',
        host_account_id: registered,
        sim_version: `125-49-${'aa'.repeat(32)}`,
        settings: '{}',
        status: 'closed',
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('room_chat_messages')
      .values([
        { room_id: room.id, account_id: registered, text: 'old', sent_at: old },
        { room_id: room.id, account_id: registered, text: 'new' },
      ])
      .execute();
    // Engine jobs: an old map job (deleted), an old verify job superseded by a
    // newer one (deleted) and that newer one (kept: the match page shows it).
    const verifiedMatch = await endedMatchWithRecord();
    const older = await createVerifyJob(db, verifiedMatch);
    await db
      .updateTable('engine_jobs')
      .set({ status: 'succeeded', completed_at: sql<Date>`now() - interval '60 days'` })
      .where('id', '=', older)
      .execute();
    const newer = await createVerifyJob(db, verifiedMatch);
    await db
      .updateTable('engine_jobs')
      .set({ status: 'succeeded', completed_at: sql<Date>`now() - interval '40 days'` })
      .where('id', '=', newer)
      .execute();
    const mapJob = await db
      .insertInto('engine_jobs')
      .values({
        kind: 'generate-map',
        sim_version: `125-49-${'aa'.repeat(32)}`,
        payload: '{}',
        status: 'succeeded',
        completed_at: sql<Date>`now() - interval '40 days'`,
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await sql`INSERT INTO notification_payloads (channel, payload, created_at)
              VALUES ('realtime', '{}', now() - interval '2 hours')`.execute(db);

    const result = await runMaintenance(database.db);
    expect(result).toMatchObject({
      deletedGuests: 1,
      deletedChatMessages: 1,
      deletedSigninAttempts: 1,
      deletedNotificationPayloads: 1,
    });
    expect(result.deletedRefreshTokens).toBeGreaterThanOrEqual(2);
    const accounts = await db
      .selectFrom('accounts')
      .select('id')
      .where('id', 'in', [oldGuest, playedGuest, freshGuest, registered, reportingGuest])
      .execute();
    expect(accounts.map((a) => a.id).sort()).toEqual(
      [playedGuest, freshGuest, registered, reportingGuest].sort(),
    );
    const tokens = await db
      .selectFrom('refresh_tokens')
      .select('token_hash')
      .where('family_id', '=', family)
      .execute();
    expect(tokens.map((t) => t.token_hash)).toEqual(['a3'.repeat(32)]);
    const jobs = await db
      .selectFrom('engine_jobs')
      .select('id')
      .where('id', 'in', [older, newer, mapJob.id])
      .execute();
    expect(jobs.map((j) => j.id)).toEqual([newer]);
  });
});

describe('blob garbage collection', () => {
  it('deletes unreferenced old blobs and unrecorded files, and keeps everything in use', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'glob2-gc-'));
    try {
      const store = new FsBlobStore(dir);
      const put = async (text: string, age: 'old' | 'new', record = true) => {
        const stored = await putContent(store, Buffer.from(text));
        if (record)
          await insertBlob(database.db, stored.sha256, stored.size, 'text/plain', 'private');
        if (age === 'old') {
          await database.db
            .updateTable('blobs')
            .set({ created_at: sql<Date>`now() - interval '30 days'` })
            .where('sha256', '=', stored.sha256)
            .execute();
          const when = new Date(Date.now() - 30 * 86_400_000);
          utimesSync(join(dir, stored.key), when, when);
        }
        return stored;
      };
      const garbage = await put('nobody needs me', 'old');
      const fresh = await put('uploaded a minute ago', 'new');
      const orphanFile = await put('stored, never recorded', 'old', false);
      const artifact = await put('a match record', 'old');
      const matchId = await endedMatchWithRecord();
      await database.db
        .insertInto('match_artifacts')
        .values({ match_id: matchId, kind: 'replay', blob_sha256: artifact.sha256 })
        .execute();
      const playedMap = await put('a map someone played', 'old');
      await database.db
        .updateTable('matches')
        .set({ map_hash: playedMap.sha256 })
        .where('id', '=', matchId)
        .execute();

      const paint = await put('a published colony paint', 'old');
      const skin = await database.db
        .insertInto('colony_skins')
        .values({ kind: 'preset', name: 'Test paint', entitlement: 'skins:test' })
        .returning('id')
        .executeTakeFirstOrThrow();
      await database.db
        .insertInto('colony_skin_versions')
        .values({
          skin_id: skin.id,
          texture_sha256: paint.sha256,
          layout: 'colony-v1',
          building_color: 123,
          manifest_sha256: 'ef'.repeat(32),
        })
        .execute();
      const studioMap = await put('a delivered AI map removed from the catalogue', 'old');
      const checkpoint = await put('a canonical AI revision source', 'old');
      const providerOutput = await put('a journaled provider image', 'old');
      const account = await createAccount(database.db, 'Studio owner');
      const map = await database.db
        .insertInto('maps')
        .values({
          owner_account_id: account,
          title: 'Studio map',
          visibility: 'private',
          made_with: 'generator',
        })
        .returning('id')
        .executeTakeFirstOrThrow();
      const thread = await database.db
        .insertInto('studio_threads')
        .values({
          account_id: account,
          title: 'Studio thread',
        })
        .returning('id')
        .executeTakeFirstOrThrow();
      const request = await database.db
        .insertInto('studio_requests')
        .values({
          id: crypto.randomUUID(),
          thread_id: thread.id,
          account_id: account,
          kind: 'generate',
          status: 'ready',
          input: {},
          map_id: map.id,
          map_hash: studioMap.sha256,
          checkpoints: { categoricalHash: checkpoint.sha256 },
        })
        .returning('id')
        .executeTakeFirstOrThrow();
      await database.db
        .insertInto('studio_attempts')
        .values({
          id: crypto.randomUUID(),
          request_id: request.id,
          stage: 'image',
          model: 'fixture',
          status: 'completed',
          input: {},
          output: { hash: providerOutput.sha256 },
        })
        .execute();
      await database.db.deleteFrom('maps').where('id', '=', map.id).execute();

      const result = await collectBlobs(database.db, store, { logger });
      expect(result.deletedOrphanFiles).toBe(1);
      expect(result.deletedBlobs).toBeGreaterThanOrEqual(1);
      expect(await store.size(garbage.key)).toBeUndefined();
      expect(await store.size(orphanFile.key)).toBeUndefined();
      for (const kept of [
        fresh,
        artifact,
        playedMap,
        paint,
        studioMap,
        checkpoint,
        providerOutput,
      ]) {
        expect(await store.size(kept.key)).toBeGreaterThan(0);
        expect(
          await database.db
            .selectFrom('blobs')
            .select('sha256')
            .where('sha256', '=', kept.sha256)
            .executeTakeFirst(),
        ).toBeDefined();
      }
      expect(contentKey(garbage.sha256)).toBe(garbage.key);
      expect(await collectBlobs(database.db, store)).toEqual({
        deletedBlobs: 0,
        deletedOrphanFiles: 0,
      });
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
});
