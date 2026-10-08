import { createHash, randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import { Credits, HiveError } from '@glob2/billing';
import { notify as notifyDatabase, type Database } from '@glob2/db';
import type {
  TerrainStudioTurn,
  TerrainStudioRevision,
  SetPackage,
  ValidateSetResult,
  TerrainStudioRequest,
  TerrainStudioThread,
  TerrainStudioEvent,
  TerrainStudioProgress,
  TerrainStudioStageId,
  TerrainStudioStageProgress,
  TerrainStudioCheck,
  TerrainStudioArtifact,
  TerrainStudioConfig,
} from '@glob2/protocol';
export interface Delivery {
  package: SetPackage;
  hash: string;
  report: ValidateSetResult;
  simVersion: string;
  text: string;
  brief?: string;
}
type Db = Kysely<Database> | Transaction<Database>;
export interface RequestRow extends Omit<TerrainStudioRequest, 'created_at' | 'input'> {
  input: TerrainStudioRequest['input'] & { base: SetPackage; config?: TerrainStudioConfig };
  account_id: string;
  checkpoints: Record<string, unknown>;
  lease: string | null;
  created_at: Date;
}
export const TERRAIN_STUDIO_CHANNEL = 'terrain-studio-updated';
export async function notify(db: Db, account: string, thread: string) {
  await notifyDatabase(db, TERRAIN_STUDIO_CHANNEL, { account, thread });
}
type EventData = TerrainStudioEvent extends infer E
  ? E extends TerrainStudioEvent
    ? Pick<E, 'type' | 'payload'>
    : never
  : never;
export const stageLabels: Record<TerrainStudioStageId, string> = {
  prepare: 'Design the set',
  artwork: 'Create artwork',
  assemble: 'Assemble the pack',
  checks: 'Validate and preview',
  ready: 'Ready to use',
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
  await sql`SELECT id FROM terrain_studio_threads WHERE id=${row.thread_id} FOR UPDATE`.execute(db);
  const old = (
    await sql`SELECT cursor FROM terrain_studio_events WHERE thread_id=${row.thread_id} AND dedup=${dedup}`.execute(
      db,
    )
  ).rows[0];
  if (old) return;
  const cursor = (
    await sql<{
      cursor: string;
    }>`UPDATE terrain_studio_threads SET event_cursor=event_cursor+1,updated_at=now() WHERE id=${row.thread_id} RETURNING event_cursor AS cursor`.execute(
      db,
    )
  ).rows[0]?.cursor;
  if (!cursor) throw new HiveError('not_found', 'No such terrain thread.');
  await sql`INSERT INTO terrain_studio_events(thread_id,cursor,request_id,dedup,type,payload) VALUES(${row.thread_id},${cursor},${row.id},${dedup},${event.type},${JSON.stringify(event.payload)}::jsonb)`.execute(
    db,
  );
  await notify(db, row.account_id, row.thread_id);
}
export async function emitState(
  db: Db,
  row: Pick<RequestRow, 'id' | 'thread_id' | 'account_id'>,
  status: TerrainStudioRequest['status'],
) {
  // State-only retries are idempotent, while changes to a service-capacity error
  // or a committed checkpoint still wake snapshot consumers.
  const current = (
    await sql<{
      error: string | null;
      checkpoints: Record<string, unknown>;
    }>`SELECT error,checkpoints FROM terrain_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(
      db,
    )
  ).rows[0];
  if (!current) throw new HiveError('not_found', 'No such terrain request.');
  const signature = `${row.id}:state:${status}:${fingerprint(current)}`;
  const last = (
    await sql<{
      cursor: string;
      dedup: string;
    }>`SELECT cursor::text AS cursor,dedup FROM terrain_studio_events WHERE request_id=${row.id} AND type='state' ORDER BY cursor DESC LIMIT 1`.execute(
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
export class TerrainStudio {
  readonly db: Kysely<Database>;
  readonly credits: Credits;
  constructor(db: Kysely<Database>) {
    this.db = db;
    this.credits = new Credits(db, 'terrain');
  }
  async own(account: string, thread: string, db: Db = this.db) {
    const row = (
      await sql<{
        id: string;
        title: string;
        brief: string;
        draftId: string;
      }>`SELECT id,title,brief,draft_id AS "draftId" FROM terrain_studio_threads WHERE id=${thread} AND account_id=${account}`.execute(
        db,
      )
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'No such terrain thread.');
    return row;
  }
  async list(account: string) {
    return (
      await sql<{
        id: string;
        title: string;
        updated_at: Date;
      }>`SELECT id,title,updated_at FROM terrain_studio_threads WHERE account_id=${account} ORDER BY updated_at DESC,id LIMIT 100`.execute(
        this.db,
      )
    ).rows;
  }
  async create(
    account: string,
    title: string,
    id: string,
    pack?: SetPackage,
    draftId?: string,
    sourceVersionId?: string,
  ) {
    return this.db.transaction().execute(async (db) => {
      const owner = await sql<{
        status: string;
      }>`SELECT status FROM accounts WHERE id=${account} FOR SHARE`.execute(db);
      if (owner.rows[0]?.status !== 'active')
        throw new HiveError('not_found', 'No such active account.');
      const old = await sql<{
        id: string;
        account_id: string;
        title: string;
        draft_id: string;
        source_version_id: string | null;
      }>`SELECT id,account_id,title,draft_id,source_version_id FROM terrain_studio_threads WHERE id=${id}`.execute(
        db,
      );
      if (old.rows[0]) {
        if (
          old.rows[0].account_id !== account ||
          old.rows[0].title !== title ||
          (draftId && old.rows[0].draft_id !== draftId) ||
          old.rows[0].source_version_id !== (sourceVersionId ?? null)
        )
          throw new HiveError('conflict', 'Project identifier already used.');
        return { id };
      }
      if (!draftId) {
        if (!pack) throw new HiveError('bad_request', 'A starting package is required.');
        draftId = pack.versionId;
        await sql`INSERT INTO asset_sets(id,owner_account_id,title,description,tags) VALUES(${pack.setId},${account},${pack.title},${pack.description},${pack.tags})`.execute(
          db,
        );
        await sql`INSERT INTO set_drafts(id,set_id,document) VALUES(${draftId},${pack.setId},${JSON.stringify(pack)}::jsonb)`.execute(
          db,
        );
      }
      const draft =
        await sql`SELECT d.id FROM set_drafts d JOIN asset_sets s ON s.id=d.set_id WHERE d.id=${draftId} AND s.owner_account_id=${account} AND d.published_version_id IS NULL AND NOT s.hidden FOR UPDATE OF d`.execute(
          db,
        );
      if (!draft.rows.length)
        throw new HiveError('not_found', 'Select an owned, unpublished draft.');
      await sql`INSERT INTO terrain_studio_threads(id,account_id,title,draft_id,source_version_id) VALUES(${id},${account},${title},${draftId},${sourceVersionId ?? null})`.execute(
        db,
      );
      return { id };
    });
  }
  async get(
    account: string,
    thread: string,
    before: { messagesBefore?: string; requestsBefore?: string } = {},
  ): Promise<TerrainStudioThread> {
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
  ): Promise<TerrainStudioThread> {
    const row = await this.own(account, thread, db);
    const cursor =
      (
        await sql<{
          cursor: string;
        }>`SELECT event_cursor::text AS cursor FROM terrain_studio_threads WHERE id=${thread}`.execute(
          db,
        )
      ).rows[0]?.cursor ?? '0';
    const messageCursor = before.messagesBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM terrain_studio_messages WHERE id=${before.messagesBefore} AND thread_id=${thread})`
      : sql``;
    const requestCursor = before.requestsBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM terrain_studio_requests WHERE id=${before.requestsBefore} AND thread_id=${thread})`
      : sql``;
    const messages = (
      await sql<
        TerrainStudioThread['messages'][number]
      >`SELECT id,role,text,created_at FROM terrain_studio_messages WHERE thread_id=${thread} ${messageCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
        db,
      )
    ).rows;
    // Conversation snapshots are private worker inputs, not duplicated in client request history.
    const requests = (
      await sql<TerrainStudioRequest>`SELECT id,thread_id,kind,status,jsonb_strip_nulls(jsonb_build_object('brief','','messages','[]'::jsonb,'pipelineVersion',input->'pipelineVersion','submission',input->'submission')) AS input,error,charged,created_at FROM terrain_studio_requests WHERE thread_id=${thread} ${requestCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
        db,
      )
    ).rows;
    return {
      id: row.id,
      title: row.title,
      brief: row.brief,
      cursor,
      messages: messages.slice(0, 200).reverse(),
      requests: requests.slice(0, 200).reverse(),
      draftId: row.draftId,
      references: (
        await sql<{
          hash: string;
          id: string;
          label: string;
        }>`SELECT DISTINCT ON (hash) hash,id,label FROM terrain_studio_artifacts WHERE thread_id=${thread} AND kind='reference' ORDER BY hash,created_at`.execute(
          db,
        )
      ).rows.map((r) => ({
        hash: r.hash,
        label: r.label,
        url: `/api/v1/terrain-studio/threads/${thread}/artifacts/${r.id}`,
      })),
      draftHistory: (
        await sql<{
          revision: number;
          title: string;
          created_at: string;
        }>`SELECT revision,document->>'title' AS title,created_at FROM terrain_studio_draft_history WHERE thread_id=${thread} ORDER BY created_at DESC`.execute(
          db,
        )
      ).rows,
      revisions: (
        await sql<TerrainStudioRevision>`SELECT request_id AS "requestId",document->>'title' AS title,applied,base_revision AS "baseRevision",report FROM terrain_studio_revisions WHERE thread_id=${thread} ORDER BY created_at DESC LIMIT 50`.execute(
          db,
        )
      ).rows,
    };
  }
  private async lockWallet(db: Db, account: string) {
    await sql`INSERT INTO terrain_wallets(account_id) VALUES(${account}) ON CONFLICT DO NOTHING`.execute(
      db,
    );
    return (
      (
        await sql<{
          balance: string;
          reserved: string;
        }>`SELECT balance,reserved FROM terrain_wallets WHERE account_id=${account} FOR UPDATE`.execute(
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
    input: TerrainStudioTurn,
    config: TerrainStudioConfig,
  ) {
    return this.db.transaction().execute(async (db) => {
      const wallet = await this.lockWallet(db, account),
        t = await this.own(account, thread, db);
      const old = await this.request(input.id, db);
      if (old) {
        if (
          old.account_id !== account ||
          old.thread_id !== thread ||
          canonical(old.checkpoints['submission']) !== canonical(input)
        )
          throw new HiveError('conflict', 'Submission identifier already used.');
        return { id: old.id };
      }
      if (Number(wallet.balance) - Number(wallet.reserved) < 1)
        throw new HiveError(
          'credits',
          'An available terrain credit is needed. Questions are free; builds cost one credit.',
        );
      const active =
        await sql`SELECT id FROM terrain_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError('conflict', 'Wait for your current request to finish.');
      const used =
        await sql`SELECT id FROM terrain_ledger WHERE id IN (${`generation:${input.id}`},${`chat:${input.id}`})`.execute(
          db,
        );
      if (used.rows.length)
        throw new HiveError('conflict', 'Submission identifier belongs to deleted history.');
      const recent = await sql<{
        n: number;
      }>`SELECT count(*)::int n FROM terrain_ledger WHERE account_id=${account} AND details->>'service'='terrain-studio-chat' AND created_at>now()-interval '1 hour'`.execute(
        db,
      );
      if ((recent.rows[0]?.n ?? 0) >= (config.chatPerHour ?? 60))
        throw new HiveError('rate_limited', 'Please wait before sending more messages.');
      const draft = await sql<{
        document: SetPackage;
        revision: number;
      }>`SELECT d.document,d.revision FROM set_drafts d JOIN asset_sets s ON s.id=d.set_id WHERE d.id=${t.draftId} AND s.owner_account_id=${account} AND d.published_version_id IS NULL AND NOT s.hidden FOR UPDATE OF d`.execute(
        db,
      );
      if (!draft.rows[0])
        throw new HiveError('not_found', 'Draft unavailable or already published.');
      if (draft.rows[0].revision !== input.expectedRevision)
        throw new HiveError('conflict', 'The draft changed. Reload before submitting.');
      for (const hash of input.references) {
        const ref =
          await sql`SELECT id FROM terrain_studio_artifacts WHERE thread_id=${thread} AND hash=${hash} AND kind='reference'`.execute(
            db,
          );
        if (!ref.rows.length)
          throw new HiveError('bad_request', 'Select reference images uploaded to this project.');
      }
      await sql`INSERT INTO terrain_ledger(id,account_id,amount,kind,details) VALUES(${`chat:${input.id}`},${account},0,'usage','{"service":"terrain-studio-chat"}'::jsonb)`.execute(
        db,
      );
      await sql`INSERT INTO terrain_studio_messages(id,thread_id,role,text) VALUES(${input.id},${thread},'user',${input.text})`.execute(
        db,
      );
      const messages = (
        await sql<{
          role: 'user' | 'assistant';
          text: string;
        }>`SELECT role,text FROM (SELECT role,text,created_at,id FROM terrain_studio_messages WHERE thread_id=${thread} ORDER BY created_at DESC,id DESC LIMIT 40) recent ORDER BY created_at,id`.execute(
          db,
        )
      ).rows;
      const snapshot = {
        brief: t.brief,
        messages,
        pipelineVersion: config.pipelineVersion,
        config,
        submission: input,
        base: draft.rows[0].document,
      };
      await sql`INSERT INTO terrain_studio_requests(id,thread_id,account_id,kind,input,checkpoints) VALUES(${input.id},${thread},${account},'chat',${JSON.stringify(snapshot)}::jsonb,${JSON.stringify({ submission: input })}::jsonb)`.execute(
        db,
      );
      const row = { id: input.id, thread_id: thread, account_id: account };
      await emitEvent(
        db,
        row,
        { type: 'message', payload: { messageId: input.id } },
        `${input.id}:user`,
      );
      await emitState(db, row, 'queued');
      return { id: input.id };
    });
  }
  async reserveBuild(row: RequestRow) {
    await this.db.transaction().execute(async (db) => {
      const wallet = await this.lockWallet(db, row.account_id);
      const current =
        await sql<RequestRow>`SELECT * FROM terrain_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(
          db,
        );
      if (current.rows[0]?.kind === 'generate') {
        row.kind = 'generate';
        return;
      }
      await this.fence(db, row);
      if (Number(wallet.balance) - Number(wallet.reserved) < 1)
        throw new HiveError('credits', 'No available terrain credit.');
      await sql`UPDATE terrain_wallets SET reserved=reserved+1 WHERE account_id=${row.account_id}`.execute(
        db,
      );
      await sql`UPDATE terrain_studio_requests SET kind='generate' WHERE id=${row.id}`.execute(db);
    });
    row.kind = 'generate';
  }
  async request(id: string, db: Db = this.db) {
    return (await sql<RequestRow>`SELECT * FROM terrain_studio_requests WHERE id=${id}`.execute(db))
      .rows[0];
  }
  async remove(account: string, thread: string) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      await this.own(account, thread, db);
      const active =
        await sql`SELECT id FROM terrain_studio_requests WHERE thread_id=${thread} AND status NOT IN ('ready','failed')`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError(
          'conflict',
          'Finish or cancel active work before deleting its history.',
        );
      await sql`DELETE FROM terrain_studio_threads WHERE id=${thread} AND account_id=${account}`.execute(
        db,
      );
    });
  }
  /** Ordinary set deletion must preserve active reservations and provider journals. */
  async removeSet(account: string, set: string, draft?: string) {
    await this.db.transaction().execute(async (db) => {
      // Submission, reservation and delivery all take this wallet first. Keeping
      // it through deletion closes the race with a new turn on this draft.
      await this.lockWallet(db, account);
      const parent =
        await sql`SELECT id FROM asset_sets WHERE id=${set} AND owner_account_id=${account} FOR UPDATE`.execute(
          db,
        );
      if (!parent.rows.length) throw new HiveError('not_found', 'Set not found.');
      const drafts =
        await sql`SELECT id FROM set_drafts WHERE set_id=${set} ${draft ? sql`AND id=${draft}` : sql``} ORDER BY id FOR UPDATE`.execute(
          db,
        );
      if (draft && !drafts.rows.length) throw new HiveError('not_found', 'Draft not found.');
      const active =
        await sql`SELECT r.id FROM terrain_studio_requests r JOIN terrain_studio_threads t ON t.id=r.thread_id JOIN set_drafts d ON d.id=t.draft_id WHERE d.set_id=${set} ${draft ? sql`AND d.id=${draft}` : sql``} AND r.status NOT IN ('ready','failed') LIMIT 1`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError(
          'conflict',
          'Finish or cancel active Terrain Studio work before deleting this draft or set.',
        );
      if (draft) await sql`DELETE FROM set_drafts WHERE id=${draft}`.execute(db);
      else await sql`DELETE FROM asset_sets WHERE id=${set}`.execute(db);
    });
  }
  async claim() {
    return this.db.transaction().execute(async (db) => {
      const row = (
        await sql<RequestRow>`SELECT * FROM terrain_studio_requests WHERE status IN ('queued','preparing','processing','importing') AND (lease_until IS NULL OR lease_until < now()) ORDER BY COALESCE(lease_until,created_at),created_at,id FOR UPDATE SKIP LOCKED LIMIT 1`.execute(
          db,
        )
      ).rows[0];
      if (!row) return undefined;
      const lease = randomUUID();
      await sql`UPDATE terrain_studio_requests SET lease=${lease},lease_until=now()+interval '15 minutes',status=CASE WHEN status='queued' THEN 'preparing' ELSE status END WHERE id=${row.id}`.execute(
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
        await sql`UPDATE terrain_studio_requests SET status=${status},checkpoints=checkpoints || ${JSON.stringify(patch)}::jsonb WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') RETURNING id`.execute(
          db,
        )
      ).rows;
      if (!updated.length) throw new HiveError('conflict', 'TerrainStudio worker lease expired.');
      await emitState(db, row, status);
    });
    row.checkpoints = { ...row.checkpoints, ...patch };
    row.status = status;
  }
  private async fence(db: Db, row: RequestRow) {
    const locked = (
      await sql`SELECT id FROM terrain_studio_requests WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') FOR UPDATE`.execute(
        db,
      )
    ).rows;
    if (!locked.length) throw new HiveError('conflict', 'TerrainStudio worker lease expired.');
  }
  async stage(
    row: RequestRow,
    id: TerrainStudioStageId,
    status: TerrainStudioStageProgress['status'],
    detail?: string,
  ) {
    const value: TerrainStudioStageProgress = {
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
          payload: TerrainStudioStageProgress;
        }>`SELECT payload FROM terrain_studio_events WHERE request_id=${row.id} AND type='stage' AND payload->>'id'=${id} ORDER BY cursor DESC LIMIT 1`.execute(
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
  async check(row: RequestRow, value: TerrainStudioCheck) {
    await this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      const previous = (
        await sql<{
          payload: TerrainStudioCheck;
        }>`SELECT payload FROM terrain_studio_events WHERE request_id=${row.id} AND type='check' AND payload->>'id'=${value.id} ORDER BY cursor DESC LIMIT 1`.execute(
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
    value: Omit<TerrainStudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      return this.registerArtifact(db, row, value);
    });
  }
  private async registerArtifact(
    db: Db,
    row: RequestRow,
    value: Omit<TerrainStudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    // Only allowlisted artifact blobs are ever allowed through the artifact route.
    const blob = (
      await sql`SELECT sha256 FROM blobs WHERE sha256=${value.hash} AND content_type IN ('image/png','application/json','text/plain')`.execute(
        db,
      )
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'The stage artifact is not available.');
    const record = (
      await sql<{
        id: string;
      }>`INSERT INTO terrain_studio_artifacts(thread_id,request_id,stage,kind,label,hash) VALUES(${row.thread_id},${row.id},${value.stage},${value.kind},${value.label},${value.hash}) ON CONFLICT(request_id,stage,kind,hash) DO UPDATE SET label=EXCLUDED.label RETURNING id`.execute(
        db,
      )
    ).rows[0];
    if (!record) throw new Error('Expected an artifact record.');
    const artifact: TerrainStudioArtifact = {
      id: record.id,
      requestId: row.id,
      stage: value.stage,
      kind: value.kind,
      label: value.label,
      url: `/api/v1/terrain-studio/threads/${row.thread_id}/artifacts/${record.id}`,
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
        status: TerrainStudioRequest['status'];
      }>`SELECT id,thread_id AS "threadId",status FROM terrain_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed') ORDER BY created_at DESC LIMIT 1`.execute(
        this.db,
      )
    ).rows[0];
  }
  async events(
    account: string,
    thread: string,
    after: string,
    limit = 100,
  ): Promise<TerrainStudioEvent[]> {
    await this.own(account, thread);
    const rows = (
      await sql<{
        id: string;
        requestId: string | null;
        type: TerrainStudioEvent['type'];
        payload: TerrainStudioEvent['payload'];
        createdAt: Date;
      }>`SELECT cursor::text AS id,request_id AS "requestId",type,payload,created_at AS "createdAt" FROM terrain_studio_events WHERE thread_id=${thread} AND cursor > ${after}::bigint ORDER BY cursor LIMIT ${limit}`.execute(
        this.db,
      )
    ).rows;
    return rows.map(
      (row) => ({ ...row, createdAt: row.createdAt.toISOString() }) as TerrainStudioEvent,
    );
  }
  async progress(account: string, thread: string, request: string): Promise<TerrainStudioProgress> {
    await this.own(account, thread);
    const row = await this.request(request);
    if (!row || row.thread_id !== thread)
      throw new HiveError('not_found', 'No such terrain request.');
    const events = (
      await sql<{
        type: TerrainStudioEvent['type'];
        payload: TerrainStudioEvent['payload'];
      }>`SELECT type,payload FROM (SELECT DISTINCT ON (type,payload->>'id') type,payload,min(cursor) OVER (PARTITION BY type,payload->>'id') AS first_cursor FROM terrain_studio_events WHERE request_id=${request} AND type IN ('stage','check') ORDER BY type,payload->>'id',cursor DESC) latest ORDER BY first_cursor`.execute(
        this.db,
      )
    ).rows;
    const stages: TerrainStudioStageProgress[] = Object.entries(stageLabels).map(([id, label]) => ({
      id: id as TerrainStudioStageId,
      label,
      status: 'pending',
    }));
    const checks: TerrainStudioCheck[] = [];
    for (const event of events) {
      if (event.type === 'stage') {
        const value = event.payload as TerrainStudioStageProgress;
        const index = stages.findIndex((s) => s.id === value.id);
        if (index >= 0) stages[index] = value;
      }
      if (event.type === 'check') checks.push(event.payload as TerrainStudioCheck);
    }
    const artifacts = (
      await sql<{
        id: string;
        stage: TerrainStudioStageId;
        kind: TerrainStudioArtifact['kind'];
        label: string;
      }>`SELECT id,stage,kind,label FROM terrain_studio_artifacts WHERE thread_id=${thread} AND request_id=${request} ORDER BY created_at,id`.execute(
        this.db,
      )
    ).rows.map((a) => ({
      id: a.id,
      requestId: request,
      stage: a.stage,
      kind: a.kind,
      label: a.label,
      url: `/api/v1/terrain-studio/threads/${thread}/artifacts/${a.id}`,
    }));
    const notes = (
      await sql<{
        payload: { text: string; attempt: number };
      }>`SELECT payload FROM terrain_studio_events WHERE request_id=${request} AND type='text' ORDER BY cursor`.execute(
        this.db,
      )
    ).rows.map((r) => r.payload);
    if (row.checkpoints['serviceLimit'])
      notes.push({
        text: 'Paused until daily provider capacity becomes available. You can cancel this request to return its reserved credit.',
        attempt: 0,
      });
    return { requestId: request, stages, artifacts, checks, notes, historical: false };
  }
  async artifactBlob(account: string, thread: string, artifact: string) {
    await this.own(account, thread);
    const blob = (
      await sql<{ storage_key: string; content_type: string }>`
      SELECT b.storage_key,b.content_type FROM terrain_studio_artifacts a JOIN blobs b ON b.sha256=a.hash
      WHERE a.id::text=${artifact} AND a.thread_id=${thread}
      AND b.content_type IN ('image/png','application/json','text/plain')`.execute(this.db)
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'No such stage artifact.');
    return blob;
  }
  async heartbeat(row: RequestRow) {
    const changed =
      await sql`UPDATE terrain_studio_requests SET lease_until=now()+interval '15 minutes'
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
    recovery?: { actor: string; reason: string },
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, row.account_id);
      await sql`SELECT id FROM terrain_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(db);
      const locked = await this.request(row.id, db);
      if (!locked) throw new HiveError('not_found', 'Request unavailable.');
      if (['ready', 'failed'].includes(locked.status)) return;
      if (recovery && locked.status !== 'uncertain')
        throw new HiveError('conflict', 'Only uncertain requests can be recovered.');
      if (cancelling && ['dispatched', 'uncertain'].includes(locked.status))
        throw new HiveError('conflict', 'Wait for the provider outcome before cancelling.');
      if (locked.lease !== row.lease) throw new HiveError('conflict', 'Worker lease expired.');
      if (result && locked.status === 'uncertain')
        throw new HiveError('conflict', 'Reconcile the provider outcome first.');
      let applied = false;
      if (result && 'package' in result) {
        if (
          locked.kind !== 'generate' ||
          !result.report.valid ||
          result.report.hash !== result.hash ||
          fingerprintBytes(result.package) !== result.hash
        )
          throw new HiveError('bad_request', 'Delivery must match a validated package.');
        const t = await this.own(row.account_id, row.thread_id, db);
        await sql`SELECT id FROM asset_sets WHERE id=${result.package.setId} FOR UPDATE`.execute(
          db,
        );
        const d = (
          await sql<{
            revision: number;
            document: SetPackage;
            published_version_id: string | null;
          }>`SELECT revision,document,published_version_id FROM set_drafts WHERE id=${t.draftId} FOR UPDATE`.execute(
            db,
          )
        ).rows[0];
        if (
          !d ||
          d.document.setId !== result.package.setId ||
          d.document.versionId !== result.package.versionId
        )
          throw new HiveError('conflict', 'Delivery does not belong to this draft.');
        const visible = (
          await sql<{
            hidden: boolean;
          }>`SELECT hidden FROM asset_sets WHERE id=${result.package.setId} FOR UPDATE`.execute(db)
        ).rows[0];
        if (!visible || visible.hidden) throw new HiveError('conflict', 'This set is unavailable.');
        applied = d.revision === row.input.submission.expectedRevision && !d.published_version_id;
        await sql`INSERT INTO terrain_studio_revisions(request_id,thread_id,base_revision,document,hash,report,sim_version,applied) VALUES(${row.id},${row.thread_id},${row.input.submission.expectedRevision},${JSON.stringify(result.package)}::jsonb,${result.hash},${JSON.stringify(result.report)}::jsonb,${result.simVersion},${applied})`.execute(
          db,
        );
        if (applied) await this.apply(db, t.draftId, result, row.thread_id);
      }
      if (result) {
        const text =
          result.text +
          ('package' in result && !applied
            ? ' Your draft changed during generation. The validated candidate is saved; review it before adopting.'
            : '');
        const messageId = randomUUID();
        await sql`INSERT INTO terrain_studio_messages(id,thread_id,role,text) VALUES(${messageId},${row.thread_id},'assistant',${text.slice(0, 16000)})`.execute(
          db,
        );
        await emitEvent(
          db,
          row,
          { type: 'message', payload: { messageId } },
          `${row.id}:assistant`,
        );
        if ('brief' in result && result.brief !== undefined)
          await sql`UPDATE terrain_studio_threads SET brief=${result.brief.slice(0, 16000)} WHERE id=${row.thread_id}`.execute(
            db,
          );
      }
      const charge = result && 'package' in result ? 1 : 0;
      if (locked.kind === 'generate') {
        await sql`UPDATE terrain_wallets SET reserved=reserved-1,balance=balance-${charge} WHERE account_id=${row.account_id}`.execute(
          db,
        );
        await sql`INSERT INTO terrain_ledger(id,account_id,amount,kind,details) VALUES(${`generation:${row.id}`},${row.account_id},${-charge},'usage',${JSON.stringify({ requestId: row.id, delivered: !!charge, returned: !charge })}::jsonb)`.execute(
          db,
        );
      }
      if (recovery)
        await sql`INSERT INTO admin_audit_log(actor_account_id,action,target_type,target_id,details) VALUES(${recovery.actor},'terrain-studio.fail','terrain-studio-request',${row.id},${JSON.stringify({ reason: recovery.reason, from: { status: locked.status, reserved: row.kind === 'generate' ? 1 : 0 }, to: { status: 'failed', reserved: 0, charged: 0 } })}::jsonb)`.execute(
          db,
        );
      await sql`UPDATE terrain_studio_requests SET status=${result ? 'ready' : 'failed'},charged=${!!charge},error=${error ?? null},completed_at=now(),lease_until=NULL WHERE id=${row.id}`.execute(
        db,
      );
      if (!result) {
        const running = (
          await sql<{
            payload: TerrainStudioStageProgress;
          }>`SELECT DISTINCT ON (payload->>'id') payload FROM terrain_studio_events WHERE request_id=${row.id} AND type='stage' ORDER BY payload->>'id',cursor DESC`.execute(
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
  private async apply(db: Db, draftId: string, result: Delivery, thread: string) {
    await this.backup(db, thread, draftId);

    await sql`UPDATE set_drafts SET document=${JSON.stringify(result.package)}::jsonb,revision=revision+1,hash=${result.hash},report=${JSON.stringify(result.report)}::jsonb,sim_version=${result.simVersion},status='valid',error=NULL,validation_job_id=NULL,updated_at=now() WHERE id=${draftId}`.execute(
      db,
    );
    await sql`UPDATE asset_sets SET title=${result.package.title},description=${result.package.description},tags=${result.package.tags},updated_at=now() WHERE id=${result.package.setId}`.execute(
      db,
    );
  }
  private async backup(db: Db, thread: string, draftId: string) {
    const size = (
      await sql<{
        bytes: string;
      }>`SELECT coalesce(sum(octet_length(h.document::text)),0)::bigint AS bytes FROM terrain_studio_draft_history h JOIN terrain_studio_threads t ON t.id=h.thread_id WHERE t.account_id=(SELECT account_id FROM terrain_studio_threads WHERE id=${thread})`.execute(
        db,
      )
    ).rows[0];
    const draft = (
      await sql<{
        bytes: number;
        exists: boolean;
      }>`SELECT octet_length(document::text)::int AS bytes,EXISTS(SELECT 1 FROM terrain_studio_draft_history h WHERE h.thread_id=${thread} AND h.revision=d.revision) AS exists FROM set_drafts d WHERE id=${draftId}`.execute(
        db,
      )
    ).rows[0];
    if (draft && !draft.exists && Number(size?.bytes ?? 0) + draft.bytes > 64 * 1024 * 1024)
      throw new HiveError(
        'conflict',
        'Private draft history is limited to 64 MiB. Export an old project before deleting its history.',
      );
    await sql`INSERT INTO terrain_studio_draft_history(thread_id,revision,document,hash,report,sim_version,status) SELECT ${thread},revision,document,hash,report,sim_version,status FROM set_drafts WHERE id=${draftId} ON CONFLICT DO NOTHING`.execute(
      db,
    );
  }
  async draftBackup(account: string, thread: string, revision: number) {
    await this.own(account, thread);
    const row = (
      await sql<{
        document: SetPackage;
      }>`SELECT document FROM terrain_studio_draft_history WHERE thread_id=${thread} AND revision=${revision}`.execute(
        this.db,
      )
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'Saved draft unavailable.');
    return row.document;
  }
  async restoreDraft(account: string, thread: string, revision: number, expectedRevision: number) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      const t = await this.own(account, thread, db);
      // Match the set-before-draft lock order used by generation and publication.
      await sql`SELECT id FROM asset_sets WHERE id=(SELECT set_id FROM set_drafts WHERE id=${t.draftId}) FOR UPDATE`.execute(
        db,
      );
      const current = (
        await sql<{
          revision: number;
          published_version_id: string | null;
        }>`SELECT revision,published_version_id FROM set_drafts WHERE id=${t.draftId} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!current || current.revision !== expectedRevision || current.published_version_id)
        throw new HiveError('conflict', 'Draft changed or was published.');
      if (
        (
          await sql`SELECT id FROM terrain_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
            db,
          )
        ).rows.length
      )
        throw new HiveError('conflict', 'Wait for active studio work before restoring.');
      const saved = (
        await sql`SELECT revision FROM terrain_studio_draft_history WHERE thread_id=${thread} AND revision=${revision}`.execute(
          db,
        )
      ).rows[0];
      if (!saved) throw new HiveError('not_found', 'Saved draft unavailable.');
      await this.backup(db, thread, t.draftId);
      await sql`UPDATE set_drafts d SET document=h.document,revision=d.revision+1,hash=h.hash,report=h.report,sim_version=h.sim_version,status=CASE WHEN h.status='pending' THEN NULL ELSE h.status END,error=NULL,validation_job_id=NULL,updated_at=now() FROM terrain_studio_draft_history h WHERE d.id=${t.draftId} AND h.thread_id=${thread} AND h.revision=${revision}`.execute(
        db,
      );
      await sql`UPDATE asset_sets s SET title=d.document->>'title',description=d.document->>'description',tags=ARRAY(SELECT jsonb_array_elements_text(d.document->'tags')),updated_at=now() FROM set_drafts d WHERE d.id=${t.draftId} AND s.id=d.set_id`.execute(
        db,
      );
      await notify(db, account, thread);
    });
  }
  async adopt(account: string, thread: string, request: string, expectedRevision: number) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      const t = await this.own(account, thread, db);
      const active =
        await sql`SELECT id FROM terrain_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError('conflict', 'Finish active work before adopting a candidate.');
      const result = (
        await sql<{
          document: SetPackage;
          hash: string;
          report: ValidateSetResult;
          sim_version: string;
          applied: boolean;
        }>`SELECT * FROM terrain_studio_revisions WHERE request_id=${request} AND thread_id=${thread} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!result) throw new HiveError('not_found', 'No such candidate.');
      await sql`SELECT id FROM asset_sets WHERE id=${result.document.setId} FOR UPDATE`.execute(db);
      const d = (
        await sql<{
          revision: number;
          published_version_id: string | null;
        }>`SELECT revision,published_version_id FROM set_drafts WHERE id=${t.draftId} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!d || d.revision !== expectedRevision || d.published_version_id)
        throw new HiveError('conflict', 'Draft changed or was published.');
      const hidden =
        await sql`SELECT id FROM asset_sets WHERE id=${result.document.setId} AND owner_account_id=${account} AND NOT hidden FOR UPDATE`.execute(
          db,
        );
      if (!hidden.rows.length) throw new HiveError('not_found', 'Set unavailable.');
      await this.apply(
        db,
        t.draftId,
        {
          package: result.document,
          hash: result.hash,
          report: result.report,
          simVersion: result.sim_version,
          text: '',
        },
        thread,
      );
      await sql`UPDATE terrain_studio_revisions SET applied=true WHERE request_id=${request}`.execute(
        db,
      );
      await notify(db, account, thread);
    });
  }
  async recoverUncertain() {
    await this.db.transaction().execute(async (db) => {
      const rows = (
        await sql<RequestRow>`SELECT * FROM terrain_studio_requests WHERE status='dispatched' AND lease_until < now() FOR UPDATE SKIP LOCKED`.execute(
          db,
        )
      ).rows;
      for (const row of rows) {
        const attempts = (
          await sql<{
            status: string;
          }>`SELECT status FROM terrain_studio_attempts WHERE request_id=${row.id}`.execute(db)
        ).rows;
        // Older workers could save a known rejection before clearing the
        // dispatched request state. Replay that failure so normal settlement
        // returns the reservation; only missing or ambiguous outcomes require
        // operator reconciliation. Never dispatch another paid call here.
        if (
          attempts.some((attempt) => attempt.status === 'failed') &&
          attempts.every((attempt) => ['completed', 'failed'].includes(attempt.status))
        ) {
          await sql`UPDATE terrain_studio_requests SET status='processing',error=NULL WHERE id=${row.id}`.execute(
            db,
          );
          await emitState(db, row, 'processing');
          continue;
        }
        await sql`UPDATE terrain_studio_attempts SET status='uncertain' WHERE request_id=${row.id} AND status='dispatched'`.execute(
          db,
        );
        await sql`UPDATE terrain_studio_requests SET status='uncertain',error='The provider outcome needs reconciliation; your credit is reserved.' WHERE id=${row.id}`.execute(
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

function fingerprintBytes(value: SetPackage) {
  return createHash('sha256').update(JSON.stringify(value)).digest('hex');
}
