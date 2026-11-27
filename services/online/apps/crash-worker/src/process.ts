// One crash report: read its bundle, symbolicate the crash, store the frames
// and file the report under the crash group of its signature
// (docs/multiplayer/crash-reports.md#the-crash-worker).
import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { sql, type Kysely } from 'kysely';
import { CRASH_PAYLOAD_ENTRIES } from '@glob2/protocol';
import {
  CRASH_GITHUB_ISSUE_TASK,
  listEntries,
  readEntry,
  ZipError,
  type BlobStore,
  type Logger,
} from '@glob2/core';
import type { Database } from '@glob2/db';
import { withScratchDir } from '@glob2/engine/process';
import {
  symbolicateMinidump,
  UnreadableCrash,
  type Stackwalker,
  type Symbolicated,
} from './minidump.ts';
import { symbolicateMetricKit } from './metrickit.ts';
import { symbolicateWeb } from './web.ts';
import { symbolicateJava } from './java.ts';
import { readErrorReport } from './error.ts';
import { signature } from './signature.ts';
import type { SymbolCache } from './symbols.ts';

const BUNDLE_MAX_BYTES = 10 * 1024 * 1024;

export interface CrashTools {
  stackwalk: Stackwalker;
  cache: SymbolCache;
  /** Scratch space for extracted dumps and stackwalk output. */
  scratch?: string;
  /** Queue a CRASH_GITHUB_ISSUE_TASK job for each new group (GitHub issues configured). */
  issues?: boolean;
}

async function readAll(stream: NodeJS.ReadableStream): Promise<Buffer> {
  const chunks: Buffer[] = [];
  let total = 0;
  for await (const chunk of stream) {
    const bytes = Buffer.from(chunk as Buffer);
    total += bytes.length;
    if (total > BUNDLE_MAX_BYTES) throw new UnreadableCrash('bundle too large');
    chunks.push(bytes);
  }
  return Buffer.concat(chunks);
}

/** Stores a failure that retrying cannot fix. */
async function fail(db: Kysely<Database>, id: string, reason: string) {
  await db
    .updateTable('crash_reports')
    .set({ status: 'failed', reason: reason.slice(0, 500), processed_at: new Date() })
    .where('id', '=', id)
    .where('status', '=', 'pending')
    .execute();
}

/** Symbolicates the crash a bundle holds. */
export async function symbolicate(
  tools: CrashTools,
  kind: keyof typeof CRASH_PAYLOAD_ENTRIES,
  bundle: Buffer,
): Promise<Symbolicated> {
  let payload: Buffer;
  try {
    const entry = listEntries(bundle, BUNDLE_MAX_BYTES).find(
      (e) => e.name === CRASH_PAYLOAD_ENTRIES[kind],
    );
    if (!entry) throw new UnreadableCrash(`bundle has no ${CRASH_PAYLOAD_ENTRIES[kind]}`);
    payload = readEntry(bundle, entry);
  } catch (error) {
    if (error instanceof ZipError) throw new UnreadableCrash(`invalid bundle: ${error.message}`);
    throw error;
  }
  if (kind === 'metrickit') return symbolicateMetricKit(tools.cache, payload.toString('utf8'));
  if (kind === 'web') return symbolicateWeb(tools.cache, payload.toString('utf8'));
  if (kind === 'java') return symbolicateJava(payload.toString('utf8'));
  if (kind === 'error') return readErrorReport(payload.toString('utf8'));
  return withScratchDir(tools.scratch, async (work) => {
    const dump = join(work, 'minidump.dmp');
    await writeFile(dump, payload);
    return symbolicateMinidump(tools.stackwalk, tools.cache, dump, work);
  });
}

/**
 * Processes report `id` if it is still pending. Unreadable input marks it
 * failed at once; other errors are rethrown for graphile-worker to retry,
 * and mark it failed on the final attempt.
 */
export async function processCrashReport(
  db: Kysely<Database>,
  blobs: BlobStore,
  tools: CrashTools,
  id: string,
  logger: Logger,
  finalAttempt = false,
): Promise<void> {
  if (!/^[A-HJ-NP-Z2-9]{8}$/.test(id)) throw new Error('Invalid crash report id');
  const row = await db
    .selectFrom('crash_reports')
    .select(['id', 'kind', 'status', 'version', 'bundle_key'])
    .where('id', '=', id)
    .executeTakeFirst();
  if (!row || row.status !== 'pending' || row.kind === 'problem') return;
  let result: Symbolicated;
  try {
    const stream = row.bundle_key ? await blobs.get(row.bundle_key) : undefined;
    if (!stream) throw new UnreadableCrash('the bundle is no longer kept');
    result = await symbolicate(tools, row.kind, await readAll(stream));
  } catch (error) {
    if (error instanceof UnreadableCrash) {
      logger.warn({ id, reason: error.message }, 'Crash report could not be read');
      await fail(db, id, error.message);
      return;
    }
    if (finalAttempt) await fail(db, id, `processing failed: ${(error as Error).message}`);
    throw error;
  } finally {
    await tools.cache
      .evict()
      .catch((err: unknown) => logger.warn({ err }, 'Symbol cache eviction failed'));
  }
  const key = result.signature ?? signature(result.frames);
  const reopens = sql`crash_groups.state = 'resolved'
    AND string_to_array(excluded.last_version, '.')::int[]
      >= string_to_array(crash_groups.fixed_in, '.')::int[]`;
  await db.transaction().execute(async (trx) => {
    // First and last releases compare numerically; crash reports only come
    // from registered release versions (crash_releases), all of them numeric.
    const group = await trx
      .insertInto('crash_groups')
      .values({ signature: key, first_version: row.version, last_version: row.version })
      .onConflict((oc) =>
        oc.column('signature').doUpdateSet({
          first_version: sql<string>`CASE WHEN string_to_array(excluded.first_version, '.')::int[]
            < string_to_array(crash_groups.first_version, '.')::int[]
            THEN excluded.first_version ELSE crash_groups.first_version END`,
          last_version: sql<string>`CASE WHEN string_to_array(excluded.last_version, '.')::int[]
            > string_to_array(crash_groups.last_version, '.')::int[]
            THEN excluded.last_version ELSE crash_groups.last_version END`,
          last_seen_at: sql<Date>`now()`,
          // A resolved group that recurs in its fixed-in release or later is
          // untriaged again (docs/multiplayer/crash-reports.md#triage).
          state: sql<'new' | 'triaged' | 'resolved'>`CASE WHEN ${reopens} THEN 'new'
            ELSE crash_groups.state END`,
          state_changed_at: sql<Date | null>`CASE WHEN ${reopens} THEN now()
            ELSE crash_groups.state_changed_at END`,
        }),
      )
      .returning(['id', sql<boolean>`xmax = 0`.as('created')])
      .executeTakeFirstOrThrow();
    if (group.created && tools.issues)
      await sql`SELECT graphile_worker.add_job(
        identifier => ${CRASH_GITHUB_ISSUE_TASK}::text,
        payload => ${JSON.stringify({ groupId: group.id })}::json,
        job_key => ${`crash-issue-${group.id}`}::text,
        max_attempts => 5)`.execute(trx);
    await trx
      .updateTable('crash_reports')
      .set({
        status: 'done',
        group_id: group.id,
        reason: result.reason.slice(0, 500),
        frames: JSON.stringify(result.frames),
        processed_at: new Date(),
      })
      .where('id', '=', id)
      .where('status', '=', 'pending')
      .execute();
  });
  logger.info({ id, group: key, frames: result.frames.length }, 'Crash report processed');
}
