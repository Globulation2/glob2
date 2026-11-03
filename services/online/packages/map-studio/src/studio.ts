import { createHash, randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import { Credits, HiveError } from '@glob2/billing';
import { notify as notifyDatabase, type Database } from '@glob2/db';
import type {
  StudioGenerate,
  StudioTurn,
  StudioRequest,
  StudioThread,
  StudioEvent,
  StudioProgress,
  StudioStageId,
  StudioStageProgress,
  StudioCheck,
  StudioArtifact,
} from '@glob2/protocol';
type Db = Kysely<Database> | Transaction<Database>;
export interface RequestRow extends Omit<StudioRequest, 'created_at'> {
  account_id: string;
  checkpoints: Record<string, unknown>;
  lease: string | null;
  created_at: Date;
}
export const STUDIO_CHANNEL = 'studio-updated';
export async function notify(db: Db, account: string, thread: string) {
  await notifyDatabase(db, STUDIO_CHANNEL, { account, thread });
}
type EventData = StudioEvent extends infer E
  ? E extends StudioEvent
    ? Pick<E, 'type' | 'payload'>
    : never
  : never;
const stageLabels: Record<StudioStageId, string> = {
  prepare: 'Prepare the design',
  terrain: 'Shape the terrain',
  build: 'Build the playable map',
  checks: 'Check the essentials',
  ready: 'Ready',
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
  await sql`SELECT id FROM studio_threads WHERE id=${row.thread_id} FOR UPDATE`.execute(db);
  const old = (
    await sql`SELECT cursor FROM studio_events WHERE thread_id=${row.thread_id} AND dedup=${dedup}`.execute(
      db,
    )
  ).rows[0];
  if (old) return;
  const cursor = (
    await sql<{
      cursor: string;
    }>`UPDATE studio_threads SET event_cursor=event_cursor+1,updated_at=now() WHERE id=${row.thread_id} RETURNING event_cursor AS cursor`.execute(
      db,
    )
  ).rows[0]?.cursor;
  if (!cursor) throw new HiveError('not_found', 'No such map thread.');
  await sql`INSERT INTO studio_events(thread_id,cursor,request_id,dedup,type,payload) VALUES(${row.thread_id},${cursor},${row.id},${dedup},${event.type},${JSON.stringify(event.payload)}::jsonb)`.execute(
    db,
  );
  await notify(db, row.account_id, row.thread_id);
}
export async function emitState(
  db: Db,
  row: Pick<RequestRow, 'id' | 'thread_id' | 'account_id'>,
  status: StudioRequest['status'],
) {
  // State-only retries are idempotent, while changes to a service-capacity error
  // or a committed checkpoint still wake snapshot consumers.
  const current = (
    await sql<{
      error: string | null;
      checkpoints: Record<string, unknown>;
    }>`SELECT error,checkpoints FROM studio_requests WHERE id=${row.id} FOR UPDATE`.execute(db)
  ).rows[0];
  if (!current) throw new HiveError('not_found', 'No such map request.');
  const signature = `${row.id}:state:${status}:${fingerprint(current)}`;
  const last = (
    await sql<{
      cursor: string;
      dedup: string;
    }>`SELECT cursor::text AS cursor,dedup FROM studio_events WHERE request_id=${row.id} AND type='state' ORDER BY cursor DESC LIMIT 1`.execute(
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
export class Studio {
  readonly db: Kysely<Database>;
  readonly credits: Credits;
  constructor(db: Kysely<Database>) {
    this.db = db;
    this.credits = new Credits(db, 'maps');
  }
  async own(account: string, thread: string, db: Db = this.db) {
    const row = (
      await sql<{
        id: string;
        title: string;
        brief: string;
      }>`SELECT id,title,brief FROM studio_threads WHERE id=${thread} AND account_id=${account}`.execute(
        db,
      )
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'No such map thread.');
    return row;
  }
  async list(account: string) {
    return (
      await sql<{
        id: string;
        title: string;
        updated_at: Date;
      }>`SELECT id,title,updated_at FROM studio_threads WHERE account_id=${account} ORDER BY updated_at DESC,id LIMIT 100`.execute(
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
        await sql<{ id: string }>`INSERT INTO studio_threads(id,account_id,title)
      VALUES(${id},${account},${title.trim() || 'New map'})
      ON CONFLICT(id) DO UPDATE SET id=EXCLUDED.id
      WHERE studio_threads.account_id=EXCLUDED.account_id AND studio_threads.title=EXCLUDED.title
      RETURNING id`.execute(db)
      ).rows[0];
      if (!row)
        throw new HiveError('conflict', 'The retry identifier belongs to another map project.');
      return row;
    });
  }
  async get(
    account: string,
    thread: string,
    before: { messagesBefore?: string; requestsBefore?: string } = {},
  ): Promise<StudioThread> {
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
  ): Promise<StudioThread> {
    const row = await this.own(account, thread, db);
    const cursor =
      (
        await sql<{
          cursor: string;
        }>`SELECT event_cursor::text AS cursor FROM studio_threads WHERE id=${thread}`.execute(db)
      ).rows[0]?.cursor ?? '0';
    const messageCursor = before.messagesBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM studio_messages WHERE id=${before.messagesBefore} AND thread_id=${thread})`
      : sql``;
    const requestCursor = before.requestsBefore
      ? sql`AND (created_at,id) < (SELECT created_at,id FROM studio_requests WHERE id=${before.requestsBefore} AND thread_id=${thread})`
      : sql``;
    const messages = (
      await sql<
        StudioThread['messages'][number]
      >`SELECT id,role,text,created_at FROM studio_messages WHERE thread_id=${thread} ${messageCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
        db,
      )
    ).rows;
    // Conversation snapshots are private worker inputs, not duplicated in client request history.
    const requests = (
      await sql<StudioRequest>`SELECT id,thread_id,kind,status,jsonb_strip_nulls(jsonb_build_object('brief','','messages','[]'::jsonb,'pipelineVersion',input->'pipelineVersion','turn',input->'turn','sourceTurnId',input->'sourceTurnId','settings',input->'settings','parent',input->'parent')) AS input,map_id,map_hash,error,charged,created_at FROM studio_requests WHERE thread_id=${thread} ${requestCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
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
    await sql`INSERT INTO map_wallets(account_id) VALUES(${account}) ON CONFLICT DO NOTHING`.execute(
      db,
    );
    return (
      (
        await sql<{
          balance: string;
          reserved: string;
        }>`SELECT balance,reserved FROM map_wallets WHERE account_id=${account} FOR UPDATE`.execute(
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
    input: StudioGenerate | StudioTurn | { id: string; text: string },
    pipelineVersion: string,
    chatPerHour = 60,
    turn = false,
  ) {
    return this.db.transaction().execute(async (db) => {
      const wallet = await this.lockWallet(db, account);
      const threadRow = await this.own(account, thread, db);
      const old = (
        await sql<RequestRow>`SELECT * FROM studio_requests WHERE id=${input.id}`.execute(db)
      ).rows[0];
      if (old) {
        const requested =
          'text' in input
            ? {
                text: input.text,
                ...(turn && 'settings' in input
                  ? { turn: true, settings: input.settings, parent: input.parent ?? null }
                  : {}),
              }
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
      if (Number(wallet.balance) - Number(wallet.reserved) < 1)
        throw new HiveError('credits', 'Buy map credits before using AI Map Studio.');
      const active = (
        await sql`SELECT id FROM studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed')`.execute(
          db,
        )
      ).rows.length;
      if (active)
        throw new HiveError('conflict', 'Wait for your current studio request to finish.');
      const submission =
        'text' in input
          ? {
              text: input.text,
              ...(turn && 'settings' in input
                ? { turn: true, settings: input.settings, parent: input.parent ?? null }
                : {}),
            }
          : { settings: input.settings, parent: input.parent ?? null };
      if (kind === 'chat' && 'text' in input) {
        const recent =
          (
            await sql<{
              n: number;
            }>`SELECT count(*)::int AS n FROM studio_requests WHERE account_id=${account} AND kind='chat' AND created_at > now()-interval '1 hour'`.execute(
              db,
            )
          ).rows[0]?.n ?? 0;
        if (recent >= chatPerHour)
          throw new HiveError('rate_limited', 'Please wait before sending more messages.');
        await sql`INSERT INTO studio_messages(id,thread_id,role,text) VALUES(${input.id},${thread},'user',${input.text})`.execute(
          db,
        );
      }
      if ('parent' in input && input.parent) {
        const parent = (
          await sql<{
            input: StudioRequest['input'];
          }>`SELECT id,input FROM studio_requests WHERE id=${input.parent} AND thread_id=${thread} AND kind='generate' AND status='ready' AND map_hash IS NOT NULL`.execute(
            db,
          )
        ).rows[0];
        if (!parent) throw new HiveError('bad_request', 'Select a delivered map in this thread.');
        if ('settings' in input && canonical(parent.input.settings) !== canonical(input.settings))
          throw new HiveError(
            'bad_request',
            'Start fresh when changing dimensions or player count.',
          );
      }
      const messages = (
        await sql<{
          role: 'user' | 'assistant';
          text: string;
        }>`SELECT role,text FROM (SELECT role,text,created_at,id FROM studio_messages WHERE thread_id=${thread} ORDER BY created_at DESC,id DESC LIMIT 40) recent ORDER BY created_at,id`.execute(
          db,
        )
      ).rows;
      if (!messages.length)
        throw new HiveError('bad_request', 'Describe the map before generating it.');
      const snapshot = {
        brief: threadRow.brief,
        messages,
        pipelineVersion,
        ...(turn ? { turn: true } : {}),
        ...('settings' in input
          ? { settings: input.settings, ...(input.parent ? { parent: input.parent } : {}) }
          : {}),
      };
      await sql`INSERT INTO studio_requests(id,thread_id,account_id,kind,input,checkpoints) VALUES(${input.id},${thread},${account},${kind},${JSON.stringify(snapshot)}::jsonb,${JSON.stringify({ submission, ...(turn ? { generationId: randomUUID() } : {}) })}::jsonb)`.execute(
        db,
      );
      if (kind === 'generate')
        await sql`UPDATE map_wallets SET reserved=reserved+1 WHERE account_id=${account}`.execute(
          db,
        );
      await sql`UPDATE studio_threads SET updated_at=now() WHERE id=${thread}`.execute(db);
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
    return (await sql<RequestRow>`SELECT * FROM studio_requests WHERE id=${id}`.execute(db))
      .rows[0];
  }
  async claim() {
    return this.db.transaction().execute(async (db) => {
      const row = (
        await sql<RequestRow>`SELECT * FROM studio_requests WHERE status IN ('queued','preparing','processing','importing') AND (lease_until IS NULL OR lease_until < now()) ORDER BY COALESCE(lease_until,created_at),created_at,id FOR UPDATE SKIP LOCKED LIMIT 1`.execute(
          db,
        )
      ).rows[0];
      if (!row) return undefined;
      const lease = randomUUID();
      await sql`UPDATE studio_requests SET lease=${lease},lease_until=now()+interval '15 minutes',status=CASE WHEN status='queued' THEN 'preparing' ELSE status END WHERE id=${row.id}`.execute(
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
        await sql`UPDATE studio_requests SET status=${status},checkpoints=checkpoints || ${JSON.stringify(patch)}::jsonb WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') RETURNING id`.execute(
          db,
        )
      ).rows;
      if (!updated.length) throw new HiveError('conflict', 'Studio worker lease expired.');
      await emitState(db, row, status);
    });
    row.checkpoints = { ...row.checkpoints, ...patch };
    row.status = status;
  }
  private async fence(db: Db, row: RequestRow) {
    const locked = (
      await sql`SELECT id FROM studio_requests WHERE id=${row.id} AND lease IS NOT DISTINCT FROM ${row.lease}::uuid AND status NOT IN ('ready','failed','uncertain') FOR UPDATE`.execute(
        db,
      )
    ).rows;
    if (!locked.length) throw new HiveError('conflict', 'Studio worker lease expired.');
  }
  async stage(
    row: RequestRow,
    id: StudioStageId,
    status: StudioStageProgress['status'],
    detail?: string,
  ) {
    const value: StudioStageProgress = {
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
          payload: StudioStageProgress;
        }>`SELECT payload FROM studio_events WHERE request_id=${row.id} AND type='stage' AND payload->>'id'=${id} ORDER BY cursor DESC LIMIT 1`.execute(
          db,
        )
      ).rows[0]?.payload;
      if (previous && ['complete', 'failed'].includes(previous.status)) return;
      await emitEvent(
        db,
        row,
        { type: 'stage', payload: value },
        `${row.id}:stage:${id}:${status}`,
      );
    });
  }
  async check(row: RequestRow, value: StudioCheck) {
    await this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      const previous = (
        await sql<{
          payload: StudioCheck;
        }>`SELECT payload FROM studio_events WHERE request_id=${row.id} AND type='check' AND payload->>'id'=${value.id} ORDER BY cursor DESC LIMIT 1`.execute(
          db,
        )
      ).rows[0]?.payload;
      if (previous && ['passed', 'failed', 'not-evaluated'].includes(previous.status)) return;
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
    value: Omit<StudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.fence(db, row);
      return this.registerArtifact(db, row, value);
    });
  }
  private async registerArtifact(
    db: Db,
    row: RequestRow,
    value: Omit<StudioArtifact, 'id' | 'requestId' | 'url'> & { hash: string },
  ) {
    // Only raster image blobs are ever allowed through the artifact route.
    const blob = (
      await sql`SELECT sha256 FROM blobs WHERE sha256=${value.hash} AND content_type IN ('image/png','image/jpeg','image/webp')`.execute(
        db,
      )
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'The stage image is not available.');
    const record = (
      await sql<{
        id: string;
      }>`INSERT INTO studio_artifacts(thread_id,request_id,stage,kind,label,hash,width,height) VALUES(${row.thread_id},${row.id},${value.stage},${value.kind},${value.label},${value.hash},${value.width ?? null},${value.height ?? null}) ON CONFLICT(request_id,stage,kind,hash) DO UPDATE SET label=EXCLUDED.label RETURNING id`.execute(
        db,
      )
    ).rows[0];
    if (!record) throw new Error('Expected an artifact record.');
    const artifact: StudioArtifact = {
      id: record.id,
      requestId: row.id,
      stage: value.stage,
      kind: value.kind,
      label: value.label,
      url: `/api/v1/map-studio/threads/${row.thread_id}/artifacts/${record.id}`,
      ...(value.width ? { width: value.width } : {}),
      ...(value.height ? { height: value.height } : {}),
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
        status: StudioRequest['status'];
      }>`SELECT id,thread_id AS "threadId",status FROM studio_requests WHERE account_id=${account} AND status NOT IN ('ready','failed') ORDER BY created_at DESC LIMIT 1`.execute(
        this.db,
      )
    ).rows[0];
  }
  async events(
    account: string,
    thread: string,
    after: string,
    limit = 100,
  ): Promise<StudioEvent[]> {
    await this.own(account, thread);
    const rows = (
      await sql<{
        id: string;
        requestId: string | null;
        type: StudioEvent['type'];
        payload: StudioEvent['payload'];
        createdAt: Date;
      }>`SELECT cursor::text AS id,request_id AS "requestId",type,payload,created_at AS "createdAt" FROM studio_events WHERE thread_id=${thread} AND cursor > ${after}::bigint ORDER BY cursor LIMIT ${limit}`.execute(
        this.db,
      )
    ).rows;
    return rows.map((row) => ({ ...row, createdAt: row.createdAt.toISOString() }) as StudioEvent);
  }
  async progress(account: string, thread: string, request: string): Promise<StudioProgress> {
    await this.own(account, thread);
    const row = await this.request(request);
    if (!row || row.thread_id !== thread) throw new HiveError('not_found', 'No such map request.');
    const events = (
      await sql<{
        type: StudioEvent['type'];
        payload: StudioEvent['payload'];
      }>`SELECT type,payload FROM (SELECT DISTINCT ON (type,payload->>'id') type,payload,min(cursor) OVER (PARTITION BY type,payload->>'id') AS first_cursor FROM studio_events WHERE request_id=${request} AND type IN ('stage','check') ORDER BY type,payload->>'id',cursor DESC) latest ORDER BY first_cursor`.execute(
        this.db,
      )
    ).rows;
    const stages: StudioStageProgress[] = Object.entries(stageLabels).map(([id, label]) => ({
      id: id as StudioStageId,
      label,
      status: 'pending',
    }));
    const checks: StudioCheck[] = [];
    for (const event of events) {
      if (event.type === 'stage') {
        const value = event.payload as StudioStageProgress;
        const index = stages.findIndex((s) => s.id === value.id);
        if (index >= 0) stages[index] = value;
      }
      if (event.type === 'check') checks.push(event.payload as StudioCheck);
    }
    const artifacts = (
      await sql<{
        id: string;
        stage: StudioStageId;
        kind: StudioArtifact['kind'];
        label: string;
        width: number | null;
        height: number | null;
      }>`SELECT id,stage,kind,label,width,height FROM studio_artifacts WHERE thread_id=${thread} AND request_id=${request} ORDER BY created_at,id`.execute(
        this.db,
      )
    ).rows.map((a) => ({
      id: a.id,
      requestId: request,
      stage: a.stage,
      kind: a.kind,
      label: a.label,
      url: `/api/v1/map-studio/threads/${thread}/artifacts/${a.id}`,
      ...(a.width ? { width: a.width } : {}),
      ...(a.height ? { height: a.height } : {}),
    }));
    // Legacy checkpoints are resolved by an explicit safe allowlist without publishing invented history.
    const historical = !(
      await sql`SELECT 1 FROM studio_events WHERE request_id=${request} LIMIT 1`.execute(this.db)
    ).rows.length;
    if (historical) {
      for (const artifact of await this.legacyArtifacts(row)) {
        if (!artifacts.some((a) => a.kind === artifact.kind && a.label === artifact.label))
          artifacts.push(artifact);
      }
      if (row.status === 'ready')
        stages[4] = {
          id: 'ready',
          label: stageLabels.ready,
          status: 'complete',
          detail: 'Delivered before detailed stage history was recorded.',
        };
    }
    return { requestId: request, stages, artifacts, checks, historical };
  }
  private async legacyArtifactHashes(row: RequestRow) {
    const values: {
      key: string;
      hash: unknown;
      stage: StudioStageId;
      kind: StudioArtifact['kind'];
      label: string;
    }[] = [
      {
        key: 'generatedHash',
        hash: row.checkpoints['generatedHash'],
        stage: 'terrain',
        kind: 'generated',
        label: 'Generated layout',
      },
      {
        key: 'overlayHash',
        hash: row.checkpoints['overlayHash'],
        stage: 'build',
        kind: 'crop',
        label: 'Crop selection',
      },
      {
        key: 'candidateHash',
        hash: row.checkpoints['candidateHash'],
        stage: 'build',
        kind: 'crop',
        label: 'Selected terrain',
      },
      {
        key: 'categoricalHash',
        hash: row.checkpoints['categoricalHash'],
        stage: 'build',
        kind: 'categorical',
        label: 'Playable terrain',
      },
      {
        key: 'previewHash',
        hash: row.checkpoints['previewHash'],
        stage: 'ready',
        kind: 'preview',
        label: 'Delivered map',
      },
    ];
    const references = (row.checkpoints['prepared'] as { references?: unknown[] } | undefined)
      ?.references;
    if (Array.isArray(references))
      references.slice(0, 20).forEach((hash, i) =>
        values.push({
          key: `reference${i}`,
          hash,
          stage: 'prepare',
          kind: 'reference',
          label: `Reference sheet ${i + 1}`,
        }),
      );
    const generated = values.find((v) => v.key === 'generatedHash');
    if (generated && !generated.hash)
      generated.hash = (
        await sql<{
          hash: unknown;
        }>`SELECT output->>'hash' AS hash FROM studio_attempts WHERE request_id=${row.id} AND stage='image' AND status='completed' ORDER BY created_at DESC LIMIT 1`.execute(
          this.db,
        )
      ).rows[0]?.hash;
    const preview = values.find((v) => v.key === 'previewHash');
    if (preview && !preview.hash && row.map_id && row.map_hash)
      preview.hash = (
        await sql<{
          hash: string | null;
        }>`SELECT preview_hash AS hash FROM map_versions WHERE map_id=${row.map_id} AND hash=${row.map_hash}`.execute(
          this.db,
        )
      ).rows[0]?.hash;
    return values.filter((v) => typeof v.hash === 'string' && /^[a-f0-9]{64}$/.test(v.hash));
  }
  private async legacyArtifacts(row: RequestRow): Promise<StudioArtifact[]> {
    return (await this.legacyArtifactHashes(row)).map((value) => {
      const id = `legacy-${row.id}-${value.key}`;
      return {
        id,
        requestId: row.id,
        stage: value.stage,
        kind: value.kind,
        label: value.label,
        url: `/api/v1/map-studio/threads/${row.thread_id}/artifacts/${id}`,
      };
    });
  }
  async artifactBlob(account: string, thread: string, artifact: string) {
    await this.own(account, thread);
    let hash: string | undefined;
    const legacy = /^legacy-([a-f0-9-]{36})-([A-Za-z0-9]+)$/.exec(artifact);
    if (legacy?.[1]) {
      const row = await this.request(legacy[1]);
      if (row?.thread_id === thread)
        hash = (await this.legacyArtifactHashes(row)).find((v) => v.key === legacy[2])?.hash as
          string | undefined;
    } else
      hash = (
        await sql<{
          hash: string;
        }>`SELECT hash FROM studio_artifacts WHERE id::text=${artifact} AND thread_id=${thread}`.execute(
          this.db,
        )
      ).rows[0]?.hash;
    if (!hash) throw new HiveError('not_found', 'No such stage image.');
    const blob = (
      await sql<{
        storage_key: string;
        content_type: string;
      }>`SELECT storage_key,content_type FROM blobs WHERE sha256=${hash} AND content_type IN ('image/png','image/jpeg','image/webp')`.execute(
        this.db,
      )
    ).rows[0];
    if (!blob) throw new HiveError('not_found', 'The stage image is not available.');
    return blob;
  }
  async finish(
    row: RequestRow,
    result?:
      | { text: string; brief?: string; action?: 'discuss' | 'build' }
      | {
          mapHash: string;
          previewHash: string;
          width: number;
          height: number;
          players: number;
          simVersion: string;
          size: number;
          previewWidth: number;
          previewHeight: number;
          provenance: Record<string, unknown>;
        },
    error?: string,
    recovery?: { actor: string; reason: string },
  ) {
    return this.db.transaction().execute(async (db) => {
      await this.lockWallet(db, row.account_id);
      const current =
        (
          await sql<RequestRow>`SELECT * FROM studio_requests WHERE id=${row.id} FOR UPDATE`.execute(
            db,
          )
        ).rows[0] ??
        (() => {
          throw new Error('Expected a database row.');
        })();
      if (['ready', 'failed'].includes(current.status)) return;
      if (recovery && current.status !== 'uncertain')
        throw new HiveError('conflict', 'Only uncertain requests can be recovered.');
      if (current.lease !== row.lease)
        throw new HiveError('conflict', 'Studio worker lease expired.');
      let mapId: string | null = null,
        mapHash: string | null = null;
      if (result && 'mapHash' in result) {
        const thread = await this.own(row.account_id, row.thread_id, db);
        mapId = randomUUID();
        mapHash = result.mapHash;
        const versionId = randomUUID();
        await sql`INSERT INTO maps(id,owner_account_id,title,visibility,made_with,authoring) VALUES(${mapId},${row.account_id},${thread.title},'private','generator',${JSON.stringify({ kind: 'ai', pipelineVersion: row.input.pipelineVersion, ...result.provenance })}::jsonb)`.execute(
          db,
        );
        await sql`INSERT INTO map_versions(id,map_id,hash,size,sim_version,validation,width,height,team_count,preview_status,preview_hash,preview_width,preview_height,uploader_account_id) VALUES(${versionId},${mapId},${mapHash},${result.size},${result.simVersion},'valid',${result.width},${result.height},${result.players},'ready',${result.previewHash},${result.previewWidth},${result.previewHeight},${row.account_id})`.execute(
          db,
        );
        await sql`UPDATE maps SET latest_version_id=${versionId} WHERE id=${mapId}`.execute(db);
      } else if (result && 'text' in result) {
        const messageId = randomUUID();
        await sql`INSERT INTO studio_messages(id,thread_id,role,text) VALUES(${messageId},${row.thread_id},'assistant',${result.text})`.execute(
          db,
        );
        await emitEvent(
          db,
          row,
          { type: 'message', payload: { messageId } },
          `${row.id}:assistant`,
        );
        if (result.brief !== undefined)
          await sql`UPDATE studio_threads SET brief=${result.brief} WHERE id=${row.thread_id}`.execute(
            db,
          );
      }
      const charge = row.kind === 'generate' && !!mapId ? 1 : 0;
      if (row.kind === 'generate') {
        await sql`UPDATE map_wallets SET reserved=reserved-1,balance=balance-${charge} WHERE account_id=${row.account_id}`.execute(
          db,
        );
        await sql`INSERT INTO map_ledger(id,account_id,amount,kind,details) VALUES(${`generation:${row.id}`},${row.account_id},${-charge},'usage',${JSON.stringify({ requestId: row.id, delivered: !!mapId, returned: !mapId })}::jsonb)`.execute(
          db,
        );
      }
      if (recovery)
        await sql`INSERT INTO admin_audit_log(actor_account_id,action,target_type,target_id,details) VALUES(${recovery.actor},'map-studio.fail','map-studio-request',${row.id},${JSON.stringify({ reason: recovery.reason, from: { status: current.status, reserved: row.kind === 'generate' ? 1 : 0 }, to: { status: 'failed', reserved: 0, charged: 0 } })}::jsonb)`.execute(
          db,
        );
      await sql`UPDATE studio_requests SET status=${result ? 'ready' : 'failed'},charged=${!!charge},map_id=${mapId},map_hash=${mapHash},error=${error ?? null},completed_at=now(),lease_until=NULL WHERE id=${row.id}`.execute(
        db,
      );
      // Complete the turn and enqueue its single build under the same wallet lock.
      // Replayed completions return above; no browser event can create a build.
      if (result && 'text' in result && current.input.turn && result.action === 'build') {
        if (!current.input.settings || typeof current.checkpoints['generationId'] !== 'string')
          throw new HiveError('bad_request', 'The turn is missing its build context.');
        const wallet = await this.lockWallet(db, row.account_id);
        if (Number(wallet.balance) - Number(wallet.reserved) < 1)
          throw new HiveError('credits', 'An available map credit is needed to build.');
        const generationId = current.checkpoints['generationId'];
        const input = {
          ...current.input,
          turn: undefined,
          sourceTurnId: current.id,
          brief: result.brief ?? current.input.brief,
          messages: [...current.input.messages, { role: 'assistant', text: result.text }],
        };
        const submission = { settings: input.settings, parent: input.parent ?? null };
        await sql`INSERT INTO studio_requests(id,thread_id,account_id,kind,input,checkpoints) VALUES(${generationId},${row.thread_id},${row.account_id},'generate',${JSON.stringify(input)}::jsonb,${JSON.stringify({ submission })}::jsonb)`.execute(
          db,
        );
        await sql`UPDATE map_wallets SET reserved=reserved+1 WHERE account_id=${row.account_id}`.execute(
          db,
        );
        await emitEvent(
          db,
          { ...row, id: generationId },
          { type: 'state', payload: { status: 'queued' } },
          `${generationId}:queued`,
        );
      }
      await sql`UPDATE studio_threads SET updated_at=now() WHERE id=${row.thread_id}`.execute(db);
      if (result && 'mapHash' in result) {
        await this.registerArtifact(db, row, {
          stage: 'ready',
          kind: 'preview',
          label: 'Ready to play',
          hash: result.previewHash,
          width: result.previewWidth,
          height: result.previewHeight,
        });
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
            payload: StudioStageProgress;
          }>`SELECT DISTINCT ON (payload->>'id') payload FROM studio_events WHERE request_id=${row.id} AND type='stage' ORDER BY payload->>'id',cursor DESC`.execute(
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
        await sql<RequestRow>`SELECT * FROM studio_requests WHERE status='dispatched' AND lease_until < now() FOR UPDATE SKIP LOCKED`.execute(
          db,
        )
      ).rows;
      for (const row of rows) {
        await sql`UPDATE studio_attempts SET status='uncertain' WHERE request_id=${row.id} AND status='dispatched'`.execute(
          db,
        );
        await sql`UPDATE studio_requests SET status='uncertain',error='The provider outcome needs reconciliation; your credit is reserved.' WHERE id=${row.id}`.execute(
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
