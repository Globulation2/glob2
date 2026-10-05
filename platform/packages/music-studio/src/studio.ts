import type { MusicMetadata } from '@glob2/protocol';
import type { ConversionResult } from '@glob2/music';
import { createHash, randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import { Credits, HiveError } from '@glob2/billing';
import { notify as notifyDatabase, type Database } from '@glob2/db';
import type {
  MusicStudioGenerate,
  MusicStudioRequest,
  MusicStudioThread,
  MusicStudioEvent,
  MusicStudioProgress,
  MusicStudioStageId,
  MusicStudioStageProgress,
  MusicStudioCheck,
  MusicStudioArtifact,
  MusicStudioConfig,
} from '@glob2/protocol';
export interface Delivery {
  metadata: MusicMetadata;
  result: ConversionResult;
  checks: MusicStudioCheck[];
  assets: { kind: string; hash: string }[];
}
type Db = Kysely<Database> | Transaction<Database>;
export interface RequestRow extends Omit<MusicStudioRequest, 'created_at'> {
  account_id: string;
  checkpoints: Record<string, unknown>;
  lease: string | null;
  created_at: Date;
}
export const MUSIC_STUDIO_CHANNEL = 'music-studio-updated';
export async function notify(db: Db, account: string, thread: string) {
  await notifyDatabase(db, MUSIC_STUDIO_CHANNEL, { account, thread });
}
type EventData = MusicStudioEvent extends infer E
  ? E extends MusicStudioEvent
    ? Pick<E, 'type' | 'payload'>
    : never
  : never;
export const stageLabels: Record<MusicStudioStageId, string> = {
  prepare: 'Compose the music',
  score: 'Check the score',
  render: 'Render the instruments',
  master: 'Mix and master',
  checks: 'Validate the audio',
  repair: 'Refine the composition',
  ready: 'Ready to listen',
};
function fingerprint(value: unknown) {
  return createHash('sha256').update(canonical(value)).digest('hex');
}
/** Call within the transaction that changes state. Thread locking serializes cursor commits. */
export async function emitEvent(
  db: Db,
  row: Pick<RequestRow, 'id' | 'thread_id' | 'account_id'>,
  event: EventData,
  dedup: string,
) {
  await sql`SELECT id FROM music_studio_threads WHERE id=${row.thread_id} FOR UPDATE`.execute(db);
  const old = (
    await sql`SELECT cursor FROM music_studio_events WHERE thread_id=${row.thread_id} AND dedup=${dedup}`.execute(
      db,
    )
  ).rows[0];
  if (old) return;
  const cursor = (
    await sql<{
      cursor: string;
    }>`UPDATE music_studio_threads SET event_cursor=event_cursor+1,updated_at=now() WHERE id=${row.thread_id} RETURNING event_cursor AS cursor`.execute(
      db,
    )
  ).rows[0]?.cursor;
  if (!cursor) throw new HiveError('not_found', 'No such music thread.');
  await sql`INSERT INTO music_studio_events(thread_id,cursor,request_id,dedup,type,payload) VALUES(${row.thread_id},${cursor},${row.id},${dedup},${event.type},${JSON.stringify(event.payload)}::jsonb)`.execute(
    db,
  );
  await notify(db, row.account_id, row.thread_id);
}
export async function emitState(
  db: Db,
  row: Pick<RequestRow, 'id' | 'thread_id' | 'account_id'>,
  status: MusicStudioRequest['status'],
) {
  // State-only retries are idempotent, while changes to a service-capacity error
  // or a committed checkpoint still wake snapshot consumers.
  const current = (
    await sql<{
      error: string | null;
      checkpoints: Record<string, unknown>;
    }>`SELECT error,checkpoints FROM music_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(
      db,
    )
  ).rows[0];
  if (!current) throw new HiveError('not_found', 'No such music request.');
  const signature = `${row.id}:state:${status}:${fingerprint(current)}`;
  const last = (
    await sql<{
      cursor: string;
      dedup: string;
    }>`SELECT cursor::text AS cursor,dedup FROM music_studio_events WHERE request_id=${row.id} AND type='state' ORDER BY cursor DESC LIMIT 1`.execute(
      db,
    )
  ).rows[0];
  if (last?.dedup.startsWith(signature + ':')) return;
  // Include the preceding transition so processing -> dispatched -> processing
  // remains visible even when a provider stage has not changed checkpoints.
  await emitEvent(
    db,
    row,
    { type: 'state', payload: { status } },
    `${signature}:${last?.cursor ?? '0'}`,
  );
}
export class MusicStudio {
  readonly db: Kysely<Database>;
  readonly credits: Credits;
  constructor(db: Kysely<Database>) {
    this.db = db;
    this.credits = new Credits(db, 'music');
  }
  async own(account: string, thread: string, db: Db = this.db) {
    const row = (
      await sql<{
        id: string;
        title: string;
        brief: string;
      }>`SELECT id,title,brief FROM music_studio_threads WHERE id=${thread} AND account_id=${account}`.execute(
        db,
      )
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'No such music thread.');
    return row;
  }
  async list(account: string) {
    return (
      await sql<{
        id: string;
        title: string;
        updated_at: Date;
      }>`SELECT id,title,updated_at FROM music_studio_threads WHERE account_id=${account} ORDER BY updated_at DESC,id LIMIT 100`.execute(
        this.db,
      )
    ).rows;
  }
  async create(account: string, title: string, id: string = randomUUID()) {
    // A client keeps this UUID before sending its first prompt, so an unknown
    // HTTP outcome can retry without creating a duplicate project.
    return this.db.transaction().execute(async (db) => {
      // Serialize creation with account deletion even if HTTP authentication
      // completed before the deletion began. The shared lock also permits other
      // project creations while preventing a deleted account gaining new data.
      const owner = (
        await sql<{
          status: string;
        }>`SELECT status FROM accounts WHERE id=${account} FOR SHARE`.execute(db)
      ).rows[0];
      if (owner?.status !== 'active') throw new HiveError('not_found', 'No such active account.');
      const row = (
        await sql<{ id: string }>`INSERT INTO music_studio_threads(id,account_id,title)
      VALUES(${id},${account},${title.trim() || 'New music'})
      ON CONFLICT(id) DO UPDATE SET id=EXCLUDED.id
      WHERE music_studio_threads.account_id=EXCLUDED.account_id AND music_studio_threads.title=EXCLUDED.title
      RETURNING id`.execute(db)
      ).rows[0];
      if (!row)
        throw new HiveError('conflict', 'The retry identifier belongs to another music project.');
      return row;
    });
  }
  async get(
    account: string,
    thread: string,
    before: { messagesBefore?: string; requestsBefore?: string } = {},
  ): Promise<MusicStudioThread> {
    return this.db
      .transaction()
      .setIsolationLevel('repeatable read')
      .execute((db) => this.snapshot(db, account, thread, before));
  }
  private async snapshot(
    db: Db,
    account: string,
    thread: string,
    before: { messagesBefore?: string; requestsBefore?: string },
  ): Promise<MusicStudioThread> {
    const row = await this.own(account, thread, db);
    const cursor =
      (
        await sql<{
          cursor: string;
        }>`SELECT event_cursor::text AS cursor FROM music_studio_threads WHERE id=${thread}`.execute(
          db,
        )
      ).rows[0]?.cursor ?? '0';
    const messageCursor = before.messagesBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM music_studio_messages WHERE id=${before.messagesBefore} AND thread_id=${thread})`
      : sql``;
    const requestCursor = before.requestsBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM music_studio_requests WHERE id=${before.requestsBefore} AND thread_id=${thread})`
      : sql``;
    const messages = (
      await sql<
        MusicStudioThread['messages'][number]
      >`SELECT id,role,text,created_at FROM music_studio_messages WHERE thread_id=${thread} ${messageCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
        db,
      )
    ).rows;
    // Conversation snapshots are private worker inputs, not duplicated in client request history.
    const requests = (
      await sql<MusicStudioRequest>`SELECT id,thread_id,kind,status,jsonb_strip_nulls(jsonb_build_object('brief','','messages','[]'::jsonb,'pipelineVersion',input->'pipelineVersion','settings',input->'settings','parent',input->'parent')) AS input,release_id,error,charged,created_at FROM music_studio_requests WHERE thread_id=${thread} ${requestCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
        db,
      )
    ).rows;
    const history = {
      ...(messages.length > 200 ? { messagesBefore: messages[199]?.id } : {}),
      ...(requests.length > 200 ? { requestsBefore: requests[199]?.id } : {}),
    };
    return {
      id: row.id,
      title: row.title,
      brief: row.brief,
      cursor,
      messages: messages.slice(0, 200).reverse(),
      requests: requests.slice(0, 200).reverse(),
      history,
    };
  }
  private async lockWallet(db: Db, account: string) {
    await sql`INSERT INTO music_wallets(account_id) VALUES(${account}) ON CONFLICT DO NOTHING`.execute(
      db,
    );
    return (
      (
        await sql<{
          balance: string;
          reserved: string;
        }>`SELECT balance,reserved FROM music_wallets WHERE account_id=${account} FOR UPDATE`.execute(
          db,
        )
      ).rows[0] ??
      (() => {
        throw new Error('Expected a database row.');
      })()
    );
  }
  async submit(
    account: string,
    thread: string,
    kind: 'chat' | 'generate',
    input: MusicStudioGenerate | { id: string; text: string },
    pipelineVersion: string,
    chatPerHour = 60,
    config?: MusicStudioConfig,
  ) {
    return this.db.transaction().execute(async (db) => {
      const wallet = await this.lockWallet(db, account);
      const threadRow = await this.own(account, thread, db);
      const old = (
        await sql<RequestRow>`SELECT * FROM music_studio_requests WHERE id=${input.id}`.execute(db)
      ).rows[0];
      if (old) {
        const requested =
          'text' in input
            ? { text: input.text }
            : { settings: input.settings, parent: input.parent ?? null };
        if (
          old.account_id !== account ||
          old.thread_id !== thread ||
          old.kind !== kind ||
          canonical(old.checkpoints['submission']) !== canonical(requested)
        )
          throw new HiveError('conflict', 'The retry identifier belongs to another request.');
        return { id: old.id };
      }
      const used =
        await sql`SELECT id FROM music_ledger WHERE id IN (${`generation:${input.id}`},${`chat:${input.id}`})`.execute(
          db,
        );
      if (used.rows.length)
        throw new HiveError('conflict', 'This submission identifier belongs to deleted history.');
      if (Number(wallet.balance) - Number(wallet.reserved) < 1)
        throw new HiveError('credits', 'Buy music credits before using AI Music Studio.');
      const active = (
        await sql`SELECT id FROM music_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
          db,
        )
      ).rows.length;
      if (active)
        throw new HiveError('conflict', 'Wait for your current studio request to finish.');
      const submission =
        'text' in input
          ? { text: input.text }
          : { settings: input.settings, parent: input.parent ?? null };
      if (kind === 'chat' && 'text' in input) {
        const recent =
          (
            await sql<{
              n: number;
            }>`SELECT count(*)::int AS n FROM music_ledger WHERE account_id=${account} AND details->>'service'='music-studio-chat' AND created_at > now()-interval '1 hour'`.execute(
              db,
            )
          ).rows[0]?.n ?? 0;
        if (recent >= chatPerHour)
          throw new HiveError('rate_limited', 'Please wait before sending more messages.');
        await sql`INSERT INTO music_ledger(id,account_id,amount,kind,details) VALUES(${`chat:${input.id}`},${account},0,'usage','{"service":"music-studio-chat"}'::jsonb)`.execute(
          db,
        );
        await sql`INSERT INTO music_studio_messages(id,thread_id,role,text) VALUES(${input.id},${thread},'user',${input.text})`.execute(
          db,
        );
      }
      if ('parent' in input && input.parent) {
        const parent = (
          await sql<{
            input: MusicStudioRequest['input'];
          }>`SELECT id,input FROM music_studio_requests WHERE id=${input.parent} AND thread_id=${thread} AND kind='generate' AND status='ready' AND release_id IS NOT NULL`.execute(
            db,
          )
        ).rows[0];
        if (!parent) throw new HiveError('bad_request', 'Select delivered music in this thread.');
        if ('settings' in input && parent.input.settings?.pipeline !== input.settings.pipeline)
          throw new HiveError('bad_request', 'Start fresh when changing the rendering pipeline.');
      }
      const messages = (
        await sql<{
          role: 'user' | 'assistant';
          text: string;
        }>`SELECT role,text FROM (SELECT role,text,created_at,id FROM music_studio_messages WHERE thread_id=${thread} ORDER BY created_at DESC,id DESC LIMIT 40) recent ORDER BY created_at,id`.execute(
          db,
        )
      ).rows;
      if (!messages.length)
        throw new HiveError('bad_request', 'Describe the music before generating it.');
      const snapshot = {
        brief: threadRow.brief,
        messages,
        pipelineVersion,
        config,
        ...('settings' in input
          ? { settings: input.settings, ...(input.parent ? { parent: input.parent } : {}) }
          : {}),
      };
      await sql`INSERT INTO music_studio_requests(id,thread_id,account_id,kind,input,checkpoints) VALUES(${input.id},${thread},${account},${kind},${JSON.stringify(snapshot)}::jsonb,${JSON.stringify({ submission })}::jsonb)`.execute(
        db,
      );
      if (kind === 'generate')
        await sql`UPDATE music_wallets SET reserved=reserved+1 WHERE account_id=${account}`.execute(
          db,
        );
      await sql`UPDATE music_studio_threads SET updated_at=now() WHERE id=${thread}`.execute(db);
      const eventRow = { id: input.id, thread_id: thread, account_id: account };
      if (kind === 'chat')
        await emitEvent(
          db,
          eventRow,
          { type: 'message', payload: { messageId: input.id } },
          `${input.id}:user`,
        );
      await emitState(db, eventRow, 'queued');
      return { id: input.id };
    });
  }
  async request(id: string, db: Db = this.db) {
    return (await sql<RequestRow>`SELECT * FROM music_studio_requests WHERE id=${id}`.execute(db))
      .rows[0];
  }
  async remove(account: string, thread: string) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      await this.own(account, thread, db);
      const active =
        await sql`SELECT id FROM music_studio_requests WHERE thread_id=${thread} AND status NOT IN ('ready','failed')`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError(
          'conflict',
          'Finish or cancel active work before deleting its history.',
        );
      await sql`DELETE FROM music_studio_threads WHERE id=${thread} AND account_id=${account}`.execute(
        db,
      );
    });
  }
  async claim() {
    return this.db.transaction().execute(async (db) => {
      const row = (
        await sql<RequestRow>`SELECT * FROM music_studio_requests WHERE status IN ('queued','preparing','processing','importing') AND (lease_until IS NULL OR lease_until < now()) ORDER BY COALESCE(lease_until,created_at),created_at,id FOR UPDATE SKIP LOCKED LIMIT 1`.execute(
          db,
        )
      ).rows[0];
      if (!row) return undefined;
      const lease = randomUUID();
      await sql`UPDATE music_studio_requests SET lease=${lease},lease_until=now()+interval '15 minutes',status=CASE WHEN status='queued' THEN 'preparing' ELSE status END WHERE id=${row.id}`.execute(
        db,
      );
      const status = row.status === 'queued' ? 'preparing' : row.status;
      await emitState(db, row, status);
      return { ...row, lease, status };
    });
  }
  async checkpoint(row: RequestRow, status: RequestRow['status'], patch: Record<string, unknown>) {
    await this.db.transaction().execute(async (db) => {
      const updated = (
        await sql`UPDATE music_studio_requests SET status=${status},checkpoints=checkpoints || ${JSON.stringify(patch)}::jsonb WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') RETURNING id`.execute(
          db,
        )
      ).rows;
      if (!updated.length) throw new HiveError('conflict', 'MusicStudio worker lease expired.');
      await emitState(db, row, status);
    });
    row.checkpoints = { ...row.checkpoints, ...patch };
    row.status = status;
  }
  private async fence(db: Db, row: RequestRow) {
    const locked = (
      await sql`SELECT id FROM music_studio_requests WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') FOR UPDATE`.execute(
        db,
      )
    ).rows;
    if (!locked.length) throw new HiveError('conflict', 'MusicStudio worker lease expired.');
  }
  async stage(
    row: RequestRow,
    id: MusicStudioStageId,
    status: MusicStudioStageProgress['status'],
    detail?: string,
  ) {
    const value: MusicStudioStageProgress = {
      id,
      label: stageLabels[id],
      status,
      ...(detail ? { detail } : {}),
      ...(status === 'complete' ? { completedAt: new Date().toISOString() } : {}),
    };
    await this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      const previous = (
        await sql<{
          payload: MusicStudioStageProgress;
        }>`SELECT payload FROM music_studio_events WHERE request_id=${row.id} AND type='stage' AND payload->>'id'=${id} ORDER BY cursor DESC LIMIT 1`.execute(
          db,
        )
      ).rows[0]?.payload;
      if (previous?.status === status && previous.detail === detail) return;
      await emitEvent(
        db,
        row,
        { type: 'stage', payload: value },
        `${row.id}:stage:${id}:${status}:${fingerprint(value)}`,
      );
    });
  }
  async check(row: RequestRow, value: MusicStudioCheck) {
    // Slow-client recovery requires every SSE frame to fit below its buffer cap.
    // Full measurements remain available in the private candidate report artifact.
    if (Buffer.byteLength(JSON.stringify(value)) > 24000) {
      const measures: MusicStudioCheck['measures'] = [];
      let size = 0;
      for (const measure of value.measures) {
        size += Buffer.byteLength(JSON.stringify(measure));
        if (size > 20000) break;
        measures.push(measure);
      }
      value = {
        ...value,
        measures,
        detail: `Showing ${measures.length} of ${value.measures.length} measurements. Open the candidate report for every finding.`,
      };
    }
    await this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      const previous = (
        await sql<{
          payload: MusicStudioCheck;
        }>`SELECT payload FROM music_studio_events WHERE request_id=${row.id} AND type='check' AND payload->>'id'=${value.id} ORDER BY cursor DESC LIMIT 1`.execute(
          db,
        )
      ).rows[0]?.payload;
      if (previous && fingerprint(previous) === fingerprint(value)) return;
      await emitEvent(
        db,
        row,
        { type: 'check', payload: value },
        `${row.id}:check:${value.id}:${fingerprint(value)}`,
      );
    });
  }
  async artifact(
    row: RequestRow,
    value: Omit<MusicStudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      return this.registerArtifact(db, row, value);
    });
  }
  private async registerArtifact(
    db: Db,
    row: RequestRow,
    value: Omit<MusicStudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    // Only allowlisted artifact blobs are ever allowed through the artifact route.
    const blob = (
      await sql`SELECT sha256 FROM blobs WHERE sha256=${value.hash} AND content_type IN ('audio/ogg','application/json','text/plain')`.execute(
        db,
      )
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'The stage artifact is not available.');
    const record = (
      await sql<{
        id: string;
      }>`INSERT INTO music_studio_artifacts(thread_id,request_id,stage,kind,label,hash) VALUES(${row.thread_id},${row.id},${value.stage},${value.kind},${value.label},${value.hash}) ON CONFLICT(request_id,stage,kind,hash) DO UPDATE SET label=EXCLUDED.label RETURNING id`.execute(
        db,
      )
    ).rows[0];
    if (!record) throw new Error('Expected an artifact record.');
    const artifact: MusicStudioArtifact = {
      id: record.id,
      requestId: row.id,
      stage: value.stage,
      kind: value.kind,
      label: value.label,
      url: `/api/v1/music-studio/threads/${row.thread_id}/artifacts/${record.id}`,
    };
    await emitEvent(
      db,
      row,
      { type: 'artifact', payload: artifact },
      `${row.id}:artifact:${record.id}`,
    );
    return artifact;
  }
  async active(account: string) {
    return (
      await sql<{
        id: string;
        threadId: string;
        status: MusicStudioRequest['status'];
      }>`SELECT id,thread_id AS "threadId",status FROM music_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed') ORDER BY created_at DESC LIMIT 1`.execute(
        this.db,
      )
    ).rows[0];
  }
  async events(
    account: string,
    thread: string,
    after: string,
    limit = 100,
  ): Promise<MusicStudioEvent[]> {
    await this.own(account, thread);
    const rows = (
      await sql<{
        id: string;
        requestId: string | null;
        type: MusicStudioEvent['type'];
        payload: MusicStudioEvent['payload'];
        createdAt: Date;
      }>`SELECT cursor::text AS id,request_id AS "requestId",type,payload,created_at AS "createdAt" FROM music_studio_events WHERE thread_id=${thread} AND cursor > ${after}::bigint ORDER BY cursor LIMIT ${limit}`.execute(
        this.db,
      )
    ).rows;
    return rows.map(
      (row) => ({ ...row, createdAt: row.createdAt.toISOString() }) as MusicStudioEvent,
    );
  }
  async progress(account: string, thread: string, request: string): Promise<MusicStudioProgress> {
    await this.own(account, thread);
    const row = await this.request(request);
    if (!row || row.thread_id !== thread)
      throw new HiveError('not_found', 'No such music request.');
    const events = (
      await sql<{
        type: MusicStudioEvent['type'];
        payload: MusicStudioEvent['payload'];
      }>`SELECT type,payload FROM (SELECT DISTINCT ON (type,payload->>'id') type,payload,min(cursor) OVER (PARTITION BY type,payload->>'id') AS first_cursor FROM music_studio_events WHERE request_id=${request} AND type IN ('stage','check') ORDER BY type,payload->>'id',cursor DESC) latest ORDER BY first_cursor`.execute(
        this.db,
      )
    ).rows;
    const stages: MusicStudioStageProgress[] = Object.entries(stageLabels).map(([id, label]) => ({
      id: id as MusicStudioStageId,
      label,
      status: 'pending',
    }));
    const checks: MusicStudioCheck[] = [];
    for (const event of events) {
      if (event.type === 'stage') {
        const value = event.payload as MusicStudioStageProgress;
        const index = stages.findIndex((s) => s.id === value.id);
        if (index >= 0) stages[index] = value;
      }
      if (event.type === 'check') checks.push(event.payload as MusicStudioCheck);
    }
    const artifacts = (
      await sql<{
        id: string;
        stage: MusicStudioStageId;
        kind: MusicStudioArtifact['kind'];
        label: string;
      }>`SELECT id,stage,kind,label FROM music_studio_artifacts WHERE thread_id=${thread} AND request_id=${request} ORDER BY created_at,id`.execute(
        this.db,
      )
    ).rows.map((a) => ({
      id: a.id,
      requestId: request,
      stage: a.stage,
      kind: a.kind,
      label: a.label,
      url: `/api/v1/music-studio/threads/${thread}/artifacts/${a.id}`,
    }));
    const notes = (
      await sql<{
        payload: { text: string; attempt: number };
      }>`SELECT payload FROM music_studio_events WHERE request_id=${request} AND type='text' ORDER BY cursor`.execute(
        this.db,
      )
    ).rows.map((r) => r.payload);
    return { requestId: request, stages, artifacts, checks, notes, historical: false };
  }
  async artifactBlob(account: string, thread: string, artifact: string) {
    await this.own(account, thread);
    const blob = (
      await sql<{ storage_key: string; content_type: string }>`
      SELECT b.storage_key,b.content_type FROM music_studio_artifacts a JOIN blobs b ON b.sha256=a.hash
      WHERE a.id::text=${artifact} AND a.thread_id=${thread}
      AND b.content_type IN ('audio/ogg','application/json','text/plain')`.execute(this.db)
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'No such stage artifact.');
    return blob;
  }
  async heartbeat(row: RequestRow) {
    const changed =
      await sql`UPDATE music_studio_requests SET lease_until=now()+interval '15 minutes'
      WHERE id=${row.id} AND lease=${row.lease} AND status NOT IN ('ready','failed','uncertain') RETURNING id`.execute(
        this.db,
      );
    return changed.rows.length === 1;
  }
  async text(row: RequestRow, text: string, attempt: number) {
    await this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      await emitEvent(
        db,
        row,
        { type: 'text', payload: { text: text.slice(0, 16000), attempt } },
        `${row.id}:text:${attempt}:${fingerprint(text)}`,
      );
    });
  }
  async cancel(account: string, thread: string, id: string) {
    await this.own(account, thread);
    const row = await this.request(id);
    if (!row || row.thread_id !== thread) throw new HiveError('not_found', 'No such request.');
    if (row.status === 'uncertain' || row.status === 'dispatched')
      throw new HiveError('conflict', 'Wait for the provider outcome before cancelling.');
    await this.finish(row, undefined, 'Cancelled. Your reserved credit was returned.', true);
  }
  async finish(
    row: RequestRow,
    result?: { text: string; brief?: string } | Delivery,

    error?: string,
    cancelling = false,
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, row.account_id);
      const current =
        (
          await sql<RequestRow>`SELECT * FROM music_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(
            db,
          )
        ).rows[0] ??
        (() => {
          throw new Error('Expected a database row.');
        })();
      if (['ready', 'failed'].includes(current.status)) return;
      if (cancelling && ['dispatched', 'uncertain'].includes(current.status))
        throw new HiveError('conflict', 'Wait for the provider outcome before cancelling.');
      if (current.lease !== row.lease)
        throw new HiveError('conflict', 'MusicStudio worker lease expired.');
      let releaseId: string | null = null;
      if (result && 'metadata' in result) {
        if (current.status === 'uncertain')
          throw new HiveError('conflict', 'Reconcile the provider outcome first.');
        releaseId = row.id;
        await sql`INSERT INTO music_releases(id,owner_id,metadata,status,result,authoring)
          VALUES(${releaseId},${row.account_id},${JSON.stringify(result.metadata)}::jsonb,'ready',
          ${JSON.stringify(result.result)}::jsonb,${JSON.stringify({
            kind: 'ai-music',
            requestId: row.id,
            pipelineVersion: row.input.pipelineVersion,
            settings: row.input.settings,
            checks: result.checks,
          })}::jsonb)`.execute(db);
        for (const asset of result.assets) {
          await sql`INSERT INTO music_assets(release_id,kind,sha256) VALUES(${releaseId},${asset.kind},${asset.hash})`.execute(
            db,
          );
        }
      } else if (result && 'text' in result) {
        const messageId = randomUUID();
        await sql`INSERT INTO music_studio_messages(id,thread_id,role,text) VALUES(${messageId},${row.thread_id},'assistant',${result.text})`.execute(
          db,
        );
        await emitEvent(
          db,
          row,
          { type: 'message', payload: { messageId } },
          `${row.id}:assistant`,
        );
        if (result.brief !== undefined)
          await sql`UPDATE music_studio_threads SET brief=${result.brief} WHERE id=${row.thread_id}`.execute(
            db,
          );
      }
      const charge = row.kind === 'generate' && !!releaseId ? 1 : 0;
      if (row.kind === 'generate') {
        await sql`UPDATE music_wallets SET reserved=reserved-1,balance=balance-${charge} WHERE account_id=${row.account_id}`.execute(
          db,
        );
        await sql`INSERT INTO music_ledger(id,account_id,amount,kind,details) VALUES(${`generation:${row.id}`},${row.account_id},${-charge},'usage',${JSON.stringify({ requestId: row.id, delivered: !!releaseId, returned: !releaseId })}::jsonb)`.execute(
          db,
        );
      }
      await sql`UPDATE music_studio_requests SET status=${result ? 'ready' : 'failed'},charged=${!!charge},release_id=${releaseId},error=${error ?? null},completed_at=now(),lease_until=NULL WHERE id=${row.id}`.execute(
        db,
      );
      await sql`UPDATE music_studio_threads SET updated_at=now() WHERE id=${row.thread_id}`.execute(
        db,
      );
      if (result && 'metadata' in result) {
        await emitEvent(
          db,
          row,
          {
            type: 'stage',
            payload: {
              id: 'ready',
              label: stageLabels.ready,
              status: 'complete',
              completedAt: new Date().toISOString(),
            },
          },
          `${row.id}:stage:ready:complete`,
        );
      }
      if (!result) {
        const running = (
          await sql<{
            payload: MusicStudioStageProgress;
          }>`SELECT DISTINCT ON (payload->>'id') payload FROM music_studio_events WHERE request_id=${row.id} AND type='stage' ORDER BY payload->>'id',cursor DESC`.execute(
            db,
          )
        ).rows.filter((e) => e.payload.status === 'running');
        for (const { payload } of running)
          await emitEvent(
            db,
            row,
            {
              type: 'stage',
              payload: {
                ...payload,
                status: 'failed',
                detail: error ?? 'Generation could not be completed.',
              },
            },
            `${row.id}:stage:${payload.id}:failed`,
          );
      }
      await emitState(db, row, result ? 'ready' : 'failed');
      await emitEvent(
        db,
        row,
        { type: 'complete', payload: { status: result ? 'ready' : 'failed' } },
        `${row.id}:complete`,
      );
    });
  }
  async recoverUncertain() {
    await this.db.transaction().execute(async (db) => {
      const rows = (
        await sql<RequestRow>`SELECT * FROM music_studio_requests WHERE status='dispatched' AND lease_until < now() FOR UPDATE SKIP LOCKED`.execute(
          db,
        )
      ).rows;
      for (const row of rows) {
        const attempts = (
          await sql<{
            status: string;
          }>`SELECT status FROM music_studio_attempts WHERE request_id=${row.id}`.execute(db)
        ).rows;
        // Older workers could save a known rejection before clearing the
        // dispatched request state. Replay that failure so normal settlement
        // returns the reservation; only missing or ambiguous outcomes require
        // operator reconciliation. Never dispatch another paid call here.
        if (
          attempts.some((attempt) => attempt.status === 'failed') &&
          attempts.every((attempt) => ['completed', 'failed'].includes(attempt.status))
        ) {
          await sql`UPDATE music_studio_requests SET status='processing',error=NULL WHERE id=${row.id}`.execute(
            db,
          );
          await emitState(db, row, 'processing');
          continue;
        }
        await sql`UPDATE music_studio_attempts SET status='uncertain' WHERE request_id=${row.id} AND status='dispatched'`.execute(
          db,
        );
        await sql`UPDATE music_studio_requests SET status='uncertain',error='The provider outcome needs reconciliation; your credit is reserved.' WHERE id=${row.id}`.execute(
          db,
        );
        await emitState(db, row, 'uncertain');
      }
    });
  }
}
function canonical(value: unknown): string {
  if (Array.isArray(value))
    return JSON.stringify(value.map((v) => JSON.parse(canonical(v)) as unknown));
  if (value && typeof value === 'object')
    return JSON.stringify(
      Object.fromEntries(
        Object.entries(value)
          .sort(([a], [b]) => a.localeCompare(b))
          .map(([k, v]) => [k, JSON.parse(canonical(v)) as unknown]),
      ),
    );
  return JSON.stringify(value);
}
