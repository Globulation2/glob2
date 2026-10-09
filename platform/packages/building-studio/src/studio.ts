import { readBuildingArchive, buildingAssetHash } from '@glob2/protocol/node';
import { createHash, randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import { Credits, HiveError } from '@glob2/billing';
export { HiveError } from '@glob2/billing';
import { notify as notifyDatabase, type Database } from '@glob2/db';
import type {
  BuildingAiStudioTurn,
  BuildingAiStudioRevision,
  BuildingPackage,
  BuildingStudioReport,
  BuildingAiStudioRequest,
  BuildingAiStudioThread,
  BuildingAiStudioEvent,
  BuildingAiStudioProgress,
  BuildingAiStudioStageId,
  BuildingAiStudioStageProgress,
  BuildingAiStudioCheck,
  BuildingAiStudioArtifact,
  BuildingAiStudioConfig,
} from '@glob2/protocol';
export interface Delivery {
  package: BuildingPackage;
  hash: string;
  archive: Buffer;
  title: string;
  report: BuildingStudioReport;
  simVersion: string;
  text: string;
  brief?: string;
}
type Db = Kysely<Database> | Transaction<Database>;
export interface RequestRow extends Omit<BuildingAiStudioRequest, 'created_at' | 'input'> {
  input: BuildingAiStudioRequest['input'] & {
    base: BuildingPackage;
    baseHash: string;
    config?: BuildingAiStudioConfig;
  };
  account_id: string;
  checkpoints: Record<string, unknown>;
  lease: string | null;
  created_at: Date;
}
export const BUILDING_STUDIO_CHANNEL = 'ai-building-studio-updated';
export async function notify(db: Db, account: string, thread: string) {
  await notifyDatabase(db, BUILDING_STUDIO_CHANNEL, { account, thread });
}
type EventData = BuildingAiStudioEvent extends infer E
  ? E extends BuildingAiStudioEvent
    ? Pick<E, 'type' | 'payload'>
    : never
  : never;
export const stageLabels: Record<BuildingAiStudioStageId, string> = {
  prepare: 'Design the building',
  artwork: 'Create artwork',
  assemble: 'Assemble the family',
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
  await sql`SELECT id FROM building_studio_threads WHERE id=${row.thread_id} FOR UPDATE`.execute(
    db,
  );
  const old = (
    await sql`SELECT cursor FROM building_studio_events WHERE thread_id=${row.thread_id} AND dedup=${dedup}`.execute(
      db,
    )
  ).rows[0];
  if (old) return;
  const cursor = (
    await sql<{
      cursor: string;
    }>`UPDATE building_studio_threads SET event_cursor=event_cursor+1,updated_at=now() WHERE id=${row.thread_id} RETURNING event_cursor AS cursor`.execute(
      db,
    )
  ).rows[0]?.cursor;
  if (!cursor) throw new HiveError('not_found', 'No such building thread.');
  await sql`INSERT INTO building_studio_events(thread_id,cursor,request_id,dedup,type,payload) VALUES(${row.thread_id},${cursor},${row.id},${dedup},${event.type},${JSON.stringify(event.payload)}::jsonb)`.execute(
    db,
  );
  await notify(db, row.account_id, row.thread_id);
}
export async function emitState(
  db: Db,
  row: Pick<RequestRow, 'id' | 'thread_id' | 'account_id'>,
  status: BuildingAiStudioRequest['status'],
) {
  // State-only retries are idempotent, while changes to a service-capacity error
  // or a committed checkpoint still wake snapshot consumers.
  const current = (
    await sql<{
      error: string | null;
      checkpoints: Record<string, unknown>;
    }>`SELECT error,checkpoints FROM building_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(
      db,
    )
  ).rows[0];
  if (!current) throw new HiveError('not_found', 'No such building request.');
  const signature = `${row.id}:state:${status}:${fingerprint(current)}`;
  const last = (
    await sql<{
      cursor: string;
      dedup: string;
    }>`SELECT cursor::text AS cursor,dedup FROM building_studio_events WHERE request_id=${row.id} AND type='state' ORDER BY cursor DESC LIMIT 1`.execute(
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
export class BuildingAiStudio {
  readonly db: Kysely<Database>;
  readonly credits: Credits;
  constructor(db: Kysely<Database>) {
    this.db = db;
    this.credits = new Credits(db, 'buildings');
  }
  async own(account: string, thread: string, db: Db = this.db) {
    const row = (
      await sql<{
        id: string;
        title: string;
        brief: string;
        draftId: string;
      }>`SELECT id,title,brief,draft_id AS "draftId" FROM building_studio_threads WHERE id=${thread} AND account_id=${account}`.execute(
        db,
      )
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'No such building thread.');
    return row;
  }
  async list(account: string) {
    return (
      await sql<{
        id: string;
        title: string;
        updated_at: Date;
      }>`SELECT id,title,updated_at FROM building_studio_threads WHERE account_id=${account} ORDER BY updated_at DESC,id LIMIT 100`.execute(
        this.db,
      )
    ).rows;
  }
  async create(account: string, title: string, id: string, archive?: Buffer, draftId?: string) {
    return this.db.transaction().execute(async (db) => {
      const owner = await sql<{
        status: string;
      }>`SELECT status FROM accounts WHERE id=${account} FOR UPDATE`.execute(db);
      if (owner.rows[0]?.status !== 'active')
        throw new HiveError('not_found', 'No such active account.');
      const old = await sql<{
        id: string;
        account_id: string;
        title: string;
        draft_id: string;
      }>`SELECT id,account_id,title,draft_id FROM building_studio_threads WHERE id=${id}`.execute(
        db,
      );
      if (old.rows[0]) {
        if (
          old.rows[0].account_id !== account ||
          old.rows[0].title !== title ||
          (draftId && old.rows[0].draft_id !== draftId)
        )
          throw new HiveError('conflict', 'Project identifier already used.');
        return { id };
      }
      const projects = (
        await sql<{
          n: number;
        }>`SELECT count(*)::int AS n FROM building_studio_threads WHERE account_id=${account}`.execute(
          db,
        )
      ).rows[0];
      if ((projects?.n ?? 0) >= 100)
        throw new HiveError(
          'conflict',
          'Your building studio has 100 projects. Delete an old project first.',
        );
      if (!draftId) {
        if (!archive) throw new HiveError('bad_request', 'A starting archive is required.');
        const used = (
          await sql<{
            n: number;
            bytes: string;
          }>`SELECT count(*)::int AS n,coalesce(sum(octet_length(archive)),0)::bigint AS bytes FROM building_drafts WHERE owner_account_id=${account}`.execute(
            db,
          )
        ).rows[0];
        if ((used?.n ?? 0) >= 100 || Number(used?.bytes ?? 0) + archive.length > 64 * 1024 * 1024)
          throw new HiveError('conflict', 'Your building workspace is full.');
        draftId = randomUUID();
        await sql`INSERT INTO building_drafts(id,owner_account_id,name,archive) VALUES(${draftId},${account},${title},${archive})`.execute(
          db,
        );
      }
      const draft =
        await sql`SELECT id FROM building_drafts WHERE id=${draftId} AND owner_account_id=${account} FOR UPDATE`.execute(
          db,
        );
      if (!draft.rows.length) throw new HiveError('not_found', 'Select an owned building draft.');
      await sql`INSERT INTO building_studio_threads(id,account_id,title,draft_id) VALUES(${id},${account},${title},${draftId})`.execute(
        db,
      );
      return { id };
    });
  }
  async get(
    account: string,
    thread: string,
    before: { messagesBefore?: string; requestsBefore?: string } = {},
  ): Promise<BuildingAiStudioThread> {
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
  ): Promise<BuildingAiStudioThread> {
    const row = await this.own(account, thread, db);
    const cursor =
      (
        await sql<{
          cursor: string;
        }>`SELECT event_cursor::text AS cursor FROM building_studio_threads WHERE id=${thread}`.execute(
          db,
        )
      ).rows[0]?.cursor ?? '0';
    const messageCursor = before.messagesBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM building_studio_messages WHERE id=${before.messagesBefore} AND thread_id=${thread})`
      : sql``;
    const requestCursor = before.requestsBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM building_studio_requests WHERE id=${before.requestsBefore} AND thread_id=${thread})`
      : sql``;
    const messages = (
      await sql<
        BuildingAiStudioThread['messages'][number]
      >`SELECT id,role,text,created_at FROM building_studio_messages WHERE thread_id=${thread} ${messageCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
        db,
      )
    ).rows;
    // Conversation snapshots are private worker inputs, not duplicated in client request history.
    const requests = (
      await sql<BuildingAiStudioRequest>`SELECT id,thread_id,kind,status,jsonb_strip_nulls(jsonb_build_object('brief','','messages','[]'::jsonb,'pipelineVersion',input->'pipelineVersion','submission',input->'submission')) AS input,error,charged,created_at FROM building_studio_requests WHERE thread_id=${thread} ${requestCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
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
        }>`SELECT DISTINCT ON (hash) hash,id,label FROM building_studio_artifacts WHERE thread_id=${thread} AND kind='reference' ORDER BY hash,created_at`.execute(
          db,
        )
      ).rows.map((r) => ({
        hash: r.hash,
        label: r.label,
        url: `/api/v1/ai-building-studio/threads/${thread}/artifacts/${r.id}`,
      })),
      draftHistory: (
        await sql<{
          revision: string;
          title: string;
          created_at: string;
        }>`SELECT revision,title,created_at FROM building_studio_draft_history WHERE thread_id=${thread} ORDER BY created_at DESC`.execute(
          db,
        )
      ).rows,
      revisions: (
        await sql<BuildingAiStudioRevision>`SELECT request_id AS "requestId",title,applied,base_revision AS "baseRevision",report,document AS package,(SELECT checkpoints->>'appliedRevision' FROM building_studio_requests WHERE id=request_id) AS "appliedRevision" FROM building_studio_revisions WHERE thread_id=${thread} ORDER BY created_at DESC LIMIT 100`.execute(
          db,
        )
      ).rows,
    };
  }
  /** Account -> wallet -> request -> draft is the shared mutation lock order.
   * Manual draft writes also lock the account first, keeping aggregate archive
   * quotas and AI delivery atomic without a wallet/draft lock inversion.
   */
  private async lockWallet(db: Db, account: string) {
    const owner = (
      await sql<{
        status: string;
      }>`SELECT status FROM accounts WHERE id=${account} FOR UPDATE`.execute(db)
    ).rows[0];
    if (!owner) throw new HiveError('not_found', 'Account unavailable.');
    await sql`INSERT INTO building_wallets(account_id) VALUES(${account}) ON CONFLICT DO NOTHING`.execute(
      db,
    );
    return (
      (
        await sql<{
          balance: string;
          reserved: string;
        }>`SELECT balance,reserved FROM building_wallets WHERE account_id=${account} FOR UPDATE`.execute(
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
    input: BuildingAiStudioTurn,
    config: BuildingAiStudioConfig,
    archiveHash: string,
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      const t = await this.own(account, thread, db);
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
      const active =
        await sql`SELECT id FROM building_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError('conflict', 'Wait for your current request to finish.');
      const used =
        await sql`SELECT id FROM building_ledger WHERE id IN (${`generation:${input.id}`},${`chat:${input.id}`})`.execute(
          db,
        );
      if (used.rows.length)
        throw new HiveError('conflict', 'Submission identifier belongs to deleted history.');
      const recent = await sql<{
        n: number;
      }>`SELECT count(*)::int n FROM building_ledger WHERE account_id=${account} AND details->>'service'='ai-building-studio-chat' AND created_at>now()-interval '1 hour'`.execute(
        db,
      );
      if ((recent.rows[0]?.n ?? 0) >= (config.chatPerHour ?? 60))
        throw new HiveError('rate_limited', 'Please wait before sending more messages.');
      const draft = await sql<{
        archive: Buffer;
        revision: string;
      }>`SELECT archive,revision FROM building_drafts WHERE id=${t.draftId} AND owner_account_id=${account} FOR UPDATE`.execute(
        db,
      );
      if (!draft.rows[0]) throw new HiveError('not_found', 'Draft unavailable.');
      if (draft.rows[0].revision !== input.expectedRevision)
        throw new HiveError('conflict', 'The draft changed. Reload before submitting.');
      if (buildingAssetHash(draft.rows[0].archive) !== archiveHash)
        throw new HiveError('conflict', 'Draft archive changed.');
      for (const hash of input.references) {
        const ref =
          await sql`SELECT id FROM building_studio_artifacts WHERE thread_id=${thread} AND hash=${hash} AND kind='reference'`.execute(
            db,
          );
        if (!ref.rows.length)
          throw new HiveError('bad_request', 'Select reference images uploaded to this project.');
      }
      await sql`INSERT INTO building_ledger(id,account_id,amount,kind,details) VALUES(${`chat:${input.id}`},${account},0,'usage','{"service":"ai-building-studio-chat"}'::jsonb)`.execute(
        db,
      );
      await sql`INSERT INTO building_studio_messages(id,thread_id,role,text) VALUES(${input.id},${thread},'user',${input.text})`.execute(
        db,
      );
      const messages = (
        await sql<{
          role: 'user' | 'assistant';
          text: string;
        }>`SELECT role,text FROM (SELECT role,text,created_at,id FROM building_studio_messages WHERE thread_id=${thread} ORDER BY created_at DESC,id DESC LIMIT 40) recent ORDER BY created_at,id`.execute(
          db,
        )
      ).rows;
      const snapshot = {
        brief: t.brief,
        messages,
        pipelineVersion: config.pipelineVersion,
        config,
        submission: input,
        base: readBuildingArchive(draft.rows[0].archive).package,
        baseHash: archiveHash,
      };
      await sql`INSERT INTO building_studio_requests(id,thread_id,account_id,kind,input,checkpoints) VALUES(${input.id},${thread},${account},'chat',${JSON.stringify(snapshot)}::jsonb,${JSON.stringify({ submission: input })}::jsonb)`.execute(
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
        await sql<RequestRow>`SELECT * FROM building_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(
          db,
        );
      // A resumed request may already have its reservation, but only the current
      // lease holder can use it. Fence before the idempotent early return too.
      await this.fence(db, row);
      if (current.rows[0]?.kind === 'generate') {
        row.kind = 'generate';
        return;
      }
      const history = (
        await sql<{
          n: number;
          bytes: string;
        }>`SELECT count(*)::int AS n,coalesce(sum(octet_length(v.archive)),0)::bigint AS bytes FROM building_studio_revisions v JOIN building_studio_threads t ON t.id=v.thread_id WHERE t.account_id=${row.account_id}`.execute(
          db,
        )
      ).rows[0];
      if ((history?.n ?? 0) >= 100 || Number(history?.bytes ?? 0) >= 64 * 1024 * 1024)
        throw new HiveError(
          'conflict',
          'Your building studio history is full. Export and delete an old project first.',
        );
      if (Number(wallet.balance) - Number(wallet.reserved) < 1)
        throw new HiveError('credits', 'No available building credit.');
      await sql`UPDATE building_wallets SET reserved=reserved+1 WHERE account_id=${row.account_id}`.execute(
        db,
      );
      await sql`UPDATE building_studio_requests SET kind='generate' WHERE id=${row.id}`.execute(db);
    });
    row.kind = 'generate';
  }
  async request(id: string, db: Db = this.db) {
    return (
      await sql<RequestRow>`SELECT * FROM building_studio_requests WHERE id=${id}`.execute(db)
    ).rows[0];
  }
  async remove(account: string, thread: string) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      await this.own(account, thread, db);
      const active =
        await sql`SELECT id FROM building_studio_requests WHERE thread_id=${thread} AND status NOT IN ('ready','failed')`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError(
          'conflict',
          'Finish or cancel active work before deleting its history.',
        );
      await sql`DELETE FROM building_studio_threads WHERE id=${thread} AND account_id=${account}`.execute(
        db,
      );
    });
  }
  async removeDraft(account: string, draft: string) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      const d =
        await sql`SELECT id FROM building_drafts WHERE id=${draft} AND owner_account_id=${account} FOR UPDATE`.execute(
          db,
        );
      if (!d.rows.length) throw new HiveError('not_found', 'Draft not found.');
      const active =
        await sql`SELECT r.id FROM building_studio_requests r JOIN building_studio_threads t ON t.id=r.thread_id WHERE t.draft_id=${draft} AND r.status NOT IN ('ready','failed') LIMIT 1`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError(
          'conflict',
          'Finish or cancel active Building Studio work before deleting this draft.',
        );
      await sql`DELETE FROM building_drafts WHERE id=${draft}`.execute(db);
    });
  }
  async claim() {
    return this.db.transaction().execute(async (db) => {
      const row = (
        await sql<RequestRow>`SELECT * FROM building_studio_requests WHERE status IN ('queued','preparing','processing','importing') AND (lease_until IS NULL OR lease_until < now()) ORDER BY COALESCE(lease_until,created_at),created_at,id FOR UPDATE SKIP LOCKED LIMIT 1`.execute(
          db,
        )
      ).rows[0];
      if (!row) return undefined;
      const lease = randomUUID();
      await sql`UPDATE building_studio_requests SET lease=${lease},lease_until=now()+interval '15 minutes',status=CASE WHEN status='queued' THEN 'preparing' ELSE status END WHERE id=${row.id}`.execute(
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
        await sql`UPDATE building_studio_requests SET status=${status},checkpoints=checkpoints || ${JSON.stringify(patch)}::jsonb WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') RETURNING id`.execute(
          db,
        )
      ).rows;
      if (!updated.length)
        throw new HiveError('conflict', 'BuildingAiStudio worker lease expired.');
      await emitState(db, row, status);
    });
    row.checkpoints = { ...row.checkpoints, ...patch };
    row.status = status;
  }
  private async fence(db: Db, row: RequestRow) {
    const locked = (
      await sql`SELECT id FROM building_studio_requests WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') FOR UPDATE`.execute(
        db,
      )
    ).rows;
    if (!locked.length) throw new HiveError('conflict', 'BuildingAiStudio worker lease expired.');
  }
  async stage(
    row: RequestRow,
    id: BuildingAiStudioStageId,
    status: BuildingAiStudioStageProgress['status'],
    detail?: string,
  ) {
    const value: BuildingAiStudioStageProgress = {
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
          payload: BuildingAiStudioStageProgress;
        }>`SELECT payload FROM building_studio_events WHERE request_id=${row.id} AND type='stage' AND payload->>'id'=${id} ORDER BY cursor DESC LIMIT 1`.execute(
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
  async check(row: RequestRow, value: BuildingAiStudioCheck) {
    await this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      const previous = (
        await sql<{
          payload: BuildingAiStudioCheck;
        }>`SELECT payload FROM building_studio_events WHERE request_id=${row.id} AND type='check' AND payload->>'id'=${value.id} ORDER BY cursor DESC LIMIT 1`.execute(
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
    value: Omit<BuildingAiStudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      return this.registerArtifact(db, row, value);
    });
  }
  private async registerArtifact(
    db: Db,
    row: RequestRow,
    value: Omit<BuildingAiStudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    // Only allowlisted artifact blobs are ever allowed through the artifact route.
    const blob = (
      await sql`SELECT sha256 FROM blobs WHERE sha256=${value.hash} AND content_type IN ('image/png','image/webp','application/zip','application/json','text/plain')`.execute(
        db,
      )
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'The stage artifact is not available.');
    const record = (
      await sql<{
        id: string;
      }>`INSERT INTO building_studio_artifacts(thread_id,request_id,stage,kind,label,hash) VALUES(${row.thread_id},${row.id},${value.stage},${value.kind},${value.label},${value.hash}) ON CONFLICT(request_id,stage,kind,hash) DO UPDATE SET label=EXCLUDED.label RETURNING id`.execute(
        db,
      )
    ).rows[0];
    if (!record) throw new Error('Expected an artifact record.');
    const artifact: BuildingAiStudioArtifact = {
      id: record.id,
      requestId: row.id,
      stage: value.stage,
      kind: value.kind,
      label: value.label,
      url: `/api/v1/ai-building-studio/threads/${row.thread_id}/artifacts/${record.id}`,
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
        status: BuildingAiStudioRequest['status'];
      }>`SELECT id,thread_id AS "threadId",status FROM building_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed') ORDER BY created_at DESC LIMIT 1`.execute(
        this.db,
      )
    ).rows[0];
  }
  async events(
    account: string,
    thread: string,
    after: string,
    limit = 100,
  ): Promise<BuildingAiStudioEvent[]> {
    await this.own(account, thread);
    const rows = (
      await sql<{
        id: string;
        requestId: string | null;
        type: BuildingAiStudioEvent['type'];
        payload: BuildingAiStudioEvent['payload'];
        createdAt: Date;
      }>`SELECT cursor::text AS id,request_id AS "requestId",type,payload,created_at AS "createdAt" FROM building_studio_events WHERE thread_id=${thread} AND cursor > ${after}::bigint ORDER BY cursor LIMIT ${limit}`.execute(
        this.db,
      )
    ).rows;
    return rows.map(
      (row) => ({ ...row, createdAt: row.createdAt.toISOString() }) as BuildingAiStudioEvent,
    );
  }
  async progress(
    account: string,
    thread: string,
    request: string,
  ): Promise<BuildingAiStudioProgress> {
    await this.own(account, thread);
    const row = await this.request(request);
    if (!row || row.thread_id !== thread)
      throw new HiveError('not_found', 'No such building request.');
    const events = (
      await sql<{
        type: BuildingAiStudioEvent['type'];
        payload: BuildingAiStudioEvent['payload'];
      }>`SELECT type,payload FROM (SELECT DISTINCT ON (type,payload->>'id') type,payload,min(cursor) OVER (PARTITION BY type,payload->>'id') AS first_cursor FROM building_studio_events WHERE request_id=${request} AND type IN ('stage','check') ORDER BY type,payload->>'id',cursor DESC) latest ORDER BY first_cursor`.execute(
        this.db,
      )
    ).rows;
    const stages: BuildingAiStudioStageProgress[] = Object.entries(stageLabels).map(
      ([id, label]) => ({
        id: id as BuildingAiStudioStageId,
        label,
        status: 'pending',
      }),
    );
    const checks: BuildingAiStudioCheck[] = [];
    for (const event of events) {
      if (event.type === 'stage') {
        const value = event.payload as BuildingAiStudioStageProgress;
        const index = stages.findIndex((s) => s.id === value.id);
        if (index >= 0) stages[index] = value;
      }
      if (event.type === 'check') checks.push(event.payload as BuildingAiStudioCheck);
    }
    const artifacts = (
      await sql<{
        id: string;
        stage: BuildingAiStudioStageId;
        kind: BuildingAiStudioArtifact['kind'];
        label: string;
      }>`SELECT id,stage,kind,label FROM building_studio_artifacts WHERE thread_id=${thread} AND request_id=${request} ORDER BY created_at,id`.execute(
        this.db,
      )
    ).rows.map((a) => ({
      id: a.id,
      requestId: request,
      stage: a.stage,
      kind: a.kind,
      label: a.label,
      url: `/api/v1/ai-building-studio/threads/${thread}/artifacts/${a.id}`,
    }));
    const notes = (
      await sql<{
        payload: { text: string; attempt: number };
      }>`SELECT payload FROM building_studio_events WHERE request_id=${request} AND type='text' ORDER BY cursor`.execute(
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
      SELECT b.storage_key,b.content_type FROM building_studio_artifacts a JOIN blobs b ON b.sha256=a.hash
      WHERE a.id::text=${artifact} AND a.thread_id=${thread}
      AND b.content_type IN ('image/png','image/webp','application/zip','application/json','text/plain')`.execute(
        this.db,
      )
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'No such stage artifact.');
    return blob;
  }
  async heartbeat(row: RequestRow) {
    const changed =
      await sql`UPDATE building_studio_requests SET lease_until=now()+interval '15 minutes'
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
      await sql`SELECT id FROM building_studio_requests WHERE id=${row.id} FOR UPDATE`.execute(db);
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
          result.report.archiveHash !== result.hash ||
          buildingAssetHash(result.archive) !== result.hash ||
          canonical(readBuildingArchive(result.archive).package) !== canonical(result.package)
        )
          throw new HiveError('bad_request', 'Delivery must match a validated package.');
        const t = await this.own(row.account_id, row.thread_id, db);
        const d = (
          await sql<{
            revision: string;
            archive: Buffer;
          }>`SELECT revision,archive FROM building_drafts WHERE id=${t.draftId} AND owner_account_id=${row.account_id} FOR UPDATE`.execute(
            db,
          )
        ).rows[0];
        if (!d || readBuildingArchive(d.archive).package.namespace !== result.package.namespace)
          throw new HiveError('conflict', 'Delivery does not belong to this draft.');
        // A manual edit never loses to generation. Persist the exact validated
        // candidate in either case; charge only after this transaction commits.
        applied = d.revision === row.input.submission.expectedRevision;
        const history = (
          await sql<{
            bytes: string;
          }>`SELECT coalesce(sum(octet_length(v.archive)),0)::bigint AS bytes FROM building_studio_revisions v JOIN building_studio_threads t ON t.id=v.thread_id WHERE t.account_id=${row.account_id}`.execute(
            db,
          )
        ).rows[0];
        if (Number(history?.bytes ?? 0) + result.archive.length > 64 * 1024 * 1024)
          throw new HiveError(
            'conflict',
            'Your building studio history is limited to 64 MiB. Export and delete an old project first.',
          );
        await sql`INSERT INTO building_studio_revisions(request_id,thread_id,base_revision,title,document,archive,hash,report,sim_version,applied) VALUES(${row.id},${row.thread_id},${row.input.submission.expectedRevision},${result.title},${JSON.stringify(result.package)}::jsonb,${result.archive},${result.hash},${JSON.stringify(result.report)}::jsonb,${result.simVersion},${applied})`.execute(
          db,
        );
        if (applied) {
          const revision = await this.apply(db, t.draftId, result, row.thread_id);
          await sql`UPDATE building_studio_requests SET checkpoints=checkpoints || ${JSON.stringify({ appliedRevision: revision })}::jsonb WHERE id=${row.id}`.execute(
            db,
          );
        }
      }
      if (result) {
        const text =
          result.text +
          ('package' in result && !applied
            ? ' Your draft changed during generation. The validated candidate is saved; review it before adopting.'
            : '');
        const messageId = randomUUID();
        await sql`INSERT INTO building_studio_messages(id,thread_id,role,text) VALUES(${messageId},${row.thread_id},'assistant',${text.slice(0, 16000)})`.execute(
          db,
        );
        await emitEvent(
          db,
          row,
          { type: 'message', payload: { messageId } },
          `${row.id}:assistant`,
        );
        if ('brief' in result && result.brief !== undefined)
          await sql`UPDATE building_studio_threads SET brief=${result.brief.slice(0, 16000)} WHERE id=${row.thread_id}`.execute(
            db,
          );
      }
      const charge = result && 'package' in result ? 1 : 0;
      if (locked.kind === 'generate') {
        await sql`UPDATE building_wallets SET reserved=reserved-1,balance=balance-${charge} WHERE account_id=${row.account_id}`.execute(
          db,
        );
        await sql`INSERT INTO building_ledger(id,account_id,amount,kind,details) VALUES(${`generation:${row.id}`},${row.account_id},${-charge},'usage',${JSON.stringify({ requestId: row.id, delivered: !!charge, returned: !charge })}::jsonb)`.execute(
          db,
        );
      }
      if (recovery)
        await sql`INSERT INTO admin_audit_log(actor_account_id,action,target_type,target_id,details) VALUES(${recovery.actor},'building-studio.fail','building-studio-request',${row.id},${JSON.stringify({ reason: recovery.reason, from: { status: locked.status, reserved: row.kind === 'generate' ? 1 : 0 }, to: { status: 'failed', reserved: 0, charged: 0 } })}::jsonb)`.execute(
          db,
        );
      await sql`UPDATE building_studio_requests SET status=${result ? 'ready' : 'failed'},charged=${!!charge},error=${error ?? null},completed_at=now(),lease_until=NULL WHERE id=${row.id}`.execute(
        db,
      );
      if (!result) {
        const running = (
          await sql<{
            payload: BuildingAiStudioStageProgress;
          }>`SELECT DISTINCT ON (payload->>'id') payload FROM building_studio_events WHERE request_id=${row.id} AND type='stage' ORDER BY payload->>'id',cursor DESC`.execute(
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
  private async apply(
    db: Db,
    draftId: string,
    result: Pick<Delivery, 'archive' | 'title'>,
    thread: string,
  ) {
    const draft = (
      await sql<{
        owner_account_id: string;
        bytes: number;
      }>`SELECT owner_account_id,octet_length(archive)::int AS bytes FROM building_drafts WHERE id=${draftId} FOR UPDATE`.execute(
        db,
      )
    ).rows[0];
    if (!draft) throw new HiveError('not_found', 'Draft unavailable.');
    const used = (
      await sql<{
        bytes: string;
      }>`SELECT coalesce(sum(octet_length(archive)),0)::bigint AS bytes FROM building_drafts WHERE owner_account_id=${draft.owner_account_id}`.execute(
        db,
      )
    ).rows[0];
    if (Number(used?.bytes ?? 0) - draft.bytes + result.archive.length > 64 * 1024 * 1024)
      throw new HiveError(
        'conflict',
        'Your building workspace is limited to 64 MiB. Export and delete an old draft first.',
      );
    await this.backup(db, thread, draftId);
    const revision = randomUUID();
    await sql`UPDATE building_drafts SET archive=${result.archive},name=${result.title},revision=${revision},updated_at=now() WHERE id=${draftId}`.execute(
      db,
    );
    return revision;
  }
  private async backup(db: Db, thread: string, draftId: string) {
    const size = (
      await sql<{
        bytes: string;
      }>`SELECT coalesce(sum(octet_length(h.archive)),0)::bigint AS bytes FROM building_studio_draft_history h JOIN building_studio_threads t ON t.id=h.thread_id WHERE t.account_id=(SELECT account_id FROM building_studio_threads WHERE id=${thread})`.execute(
        db,
      )
    ).rows[0];
    const draft = (
      await sql<{
        bytes: number;
        exists: boolean;
      }>`SELECT octet_length(archive)::int AS bytes,EXISTS(SELECT 1 FROM building_studio_draft_history h WHERE h.thread_id=${thread} AND h.revision=d.revision) AS exists FROM building_drafts d WHERE id=${draftId}`.execute(
        db,
      )
    ).rows[0];
    if (draft && !draft.exists && Number(size?.bytes ?? 0) + draft.bytes > 64 * 1024 * 1024)
      throw new HiveError(
        'conflict',
        'Private draft history is limited to 64 MiB. Export an old project before deleting its history.',
      );
    await sql`INSERT INTO building_studio_draft_history(thread_id,revision,title,archive) SELECT ${thread},revision,name,archive FROM building_drafts WHERE id=${draftId} ON CONFLICT DO NOTHING`.execute(
      db,
    );
  }
  async draftBackup(account: string, thread: string, revision: string) {
    await this.own(account, thread);
    const row = (
      await sql<{
        archive: Buffer;
        title: string;
      }>`SELECT archive,title FROM building_studio_draft_history WHERE thread_id=${thread} AND revision=${revision}`.execute(
        this.db,
      )
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'Saved draft unavailable.');
    return row;
  }
  async restoreDraft(account: string, thread: string, revision: string, expectedRevision: string) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      const t = await this.own(account, thread, db);
      const current = (
        await sql<{
          revision: string;
        }>`SELECT revision FROM building_drafts WHERE id=${t.draftId} FOR UPDATE`.execute(db)
      ).rows[0];
      if (!current || current.revision !== expectedRevision)
        throw new HiveError('conflict', 'Draft changed.');
      const active = (
        await sql`SELECT id FROM building_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
          db,
        )
      ).rows;
      if (active.length)
        throw new HiveError('conflict', 'Wait for active studio work before restoring.');
      const saved = (
        await sql<{
          archive: Buffer;
          title: string;
        }>`SELECT archive,title FROM building_studio_draft_history WHERE thread_id=${thread} AND revision=${revision}`.execute(
          db,
        )
      ).rows[0];
      if (!saved) throw new HiveError('not_found', 'Saved draft unavailable.');
      await this.apply(db, t.draftId, saved, thread);
      await notify(db, account, thread);
    });
  }
  async adopt(
    account: string,
    thread: string,
    request: string,
    expectedRevision: string,
    archive: Buffer,
  ) {
    await this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, account);
      const t = await this.own(account, thread, db);
      const active =
        await sql`SELECT id FROM building_studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
          db,
        );
      if (active.rows.length)
        throw new HiveError('conflict', 'Finish active work before adopting a candidate.');
      const result = (
        await sql<{
          document: BuildingPackage;
          title: string;
          hash: string;
          report: BuildingStudioReport;
        }>`SELECT * FROM building_studio_revisions WHERE request_id=${request} AND thread_id=${thread} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!result) throw new HiveError('not_found', 'No such candidate.');
      const d = (
        await sql<{
          revision: string;
          archive: Buffer;
        }>`SELECT revision,archive FROM building_drafts WHERE id=${t.draftId} AND owner_account_id=${account} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!d || d.revision !== expectedRevision) throw new HiveError('conflict', 'Draft changed.');
      if (
        buildingAssetHash(archive) !== result.hash ||
        !result.report.valid ||
        result.report.archiveHash !== result.hash ||
        readBuildingArchive(d.archive).package.namespace !==
          readBuildingArchive(archive).package.namespace
      )
        throw new HiveError('conflict', 'Candidate archive does not match this draft.');
      const appliedRevision = await this.apply(
        db,
        t.draftId,
        { archive, title: result.title },
        thread,
      );
      await sql`UPDATE building_studio_requests SET checkpoints=checkpoints || ${JSON.stringify({ appliedRevision })}::jsonb WHERE id=${request}`.execute(
        db,
      );
      await sql`UPDATE building_studio_revisions SET applied=true WHERE request_id=${request}`.execute(
        db,
      );
      await notify(db, account, thread);
    });
  }
  async recoverUncertain() {
    await this.db.transaction().execute(async (db) => {
      const rows = (
        await sql<RequestRow>`SELECT * FROM building_studio_requests WHERE status='dispatched' AND lease_until < now() FOR UPDATE SKIP LOCKED`.execute(
          db,
        )
      ).rows;
      for (const row of rows) {
        const attempts = (
          await sql<{
            status: string;
          }>`SELECT status FROM building_studio_attempts WHERE request_id=${row.id}`.execute(db)
        ).rows;
        // Older workers could save a known rejection before clearing the
        // dispatched request state. Replay that failure so normal settlement
        // returns the reservation; only missing or ambiguous outcomes require
        // operator reconciliation. Never dispatch another paid call here.
        if (
          attempts.some((attempt) => attempt.status === 'failed') &&
          attempts.every((attempt) => ['completed', 'failed'].includes(attempt.status))
        ) {
          await sql`UPDATE building_studio_requests SET status='processing',error=NULL WHERE id=${row.id}`.execute(
            db,
          );
          await emitState(db, row, 'processing');
          continue;
        }
        await sql`UPDATE building_studio_attempts SET status='uncertain' WHERE request_id=${row.id} AND status='dispatched'`.execute(
          db,
        );
        await sql`UPDATE building_studio_requests SET status='uncertain',error='The provider outcome needs reconciliation; your credit is reserved.' WHERE id=${row.id}`.execute(
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
