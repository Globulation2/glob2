import { randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import { Credits, HiveError } from '@glob2/billing';
import { notify as notifyDatabase, type Database } from '@glob2/db';
import type { StudioGenerate, StudioRequest, StudioThread } from '@glob2/protocol';
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
  async create(account: string, title: string) {
    return (
      (
        await sql<{
          id: string;
        }>`INSERT INTO studio_threads(account_id,title) VALUES(${account},${title.trim() || 'New map'}) RETURNING id`.execute(
          this.db,
        )
      ).rows[0] ??
      (() => {
        throw new Error('Expected a database row.');
      })()
    );
  }
  async get(
    account: string,
    thread: string,
    before: { messagesBefore?: string; requestsBefore?: string } = {},
  ): Promise<StudioThread> {
    const row = await this.own(account, thread);
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
        this.db,
      )
    ).rows;
    // Conversation snapshots are private worker inputs, not duplicated in client request history.
    const requests = (
      await sql<StudioRequest>`SELECT id,thread_id,kind,status,jsonb_strip_nulls(jsonb_build_object('brief','','messages','[]'::jsonb,'pipelineVersion',input->'pipelineVersion','settings',input->'settings','parent',input->'parent')) AS input,map_id,map_hash,error,charged,created_at FROM studio_requests WHERE thread_id=${thread} ${requestCursor} ORDER BY created_at DESC,id DESC LIMIT 201`.execute(
        this.db,
      )
    ).rows;
    const history = {
      ...(messages.length > 200 ? { messagesBefore: messages[199]?.id } : {}),
      ...(requests.length > 200 ? { requestsBefore: requests[199]?.id } : {}),
    };
    return {
      id: row.id,
      title: row.title,
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
    input: StudioGenerate | { id: string; text: string },
    pipelineVersion: string,
    chatPerHour = 60,
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
          ? { text: input.text }
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
        ...('settings' in input
          ? { settings: input.settings, ...(input.parent ? { parent: input.parent } : {}) }
          : {}),
      };
      await sql`INSERT INTO studio_requests(id,thread_id,account_id,kind,input,checkpoints) VALUES(${input.id},${thread},${account},${kind},${JSON.stringify(snapshot)}::jsonb,${JSON.stringify({ submission })}::jsonb)`.execute(
        db,
      );
      if (kind === 'generate')
        await sql`UPDATE map_wallets SET reserved=reserved+1 WHERE account_id=${account}`.execute(
          db,
        );
      await sql`UPDATE studio_threads SET updated_at=now() WHERE id=${thread}`.execute(db);
      await notify(db, account, thread);
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
      return { ...row, lease };
    });
  }
  async checkpoint(row: RequestRow, status: RequestRow['status'], patch: Record<string, unknown>) {
    const updated = (
      await sql`UPDATE studio_requests SET status=${status},checkpoints=checkpoints || ${JSON.stringify(patch)}::jsonb WHERE id=${row.id} AND lease=${row.lease} AND status NOT IN ('ready','failed','uncertain') RETURNING id`.execute(
        this.db,
      )
    ).rows;
    if (!updated.length) throw new HiveError('conflict', 'Studio worker lease expired.');
    row.checkpoints = { ...row.checkpoints, ...patch };
    row.status = status;
    await notify(this.db, row.account_id, row.thread_id);
  }
  async finish(
    row: RequestRow,
    result?:
      | { text: string; brief?: string }
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
        await sql`INSERT INTO studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${row.thread_id},'assistant',${result.text})`.execute(
          db,
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
      await sql`UPDATE studio_requests SET status=${result ? 'ready' : 'failed'},charged=${!!charge},map_id=${mapId},map_hash=${mapHash},error=${error ?? null},completed_at=now(),lease_until=NULL WHERE id=${row.id}`.execute(
        db,
      );
      await sql`UPDATE studio_threads SET updated_at=now() WHERE id=${row.thread_id}`.execute(db);
      await notify(db, row.account_id, row.thread_id);
    });
  }
  async recoverUncertain() {
    await this.db.transaction().execute(async (db) => {
      const rows = (
        await sql<{
          id: string;
        }>`SELECT id FROM studio_requests WHERE status='dispatched' AND lease_until < now() FOR UPDATE SKIP LOCKED`.execute(
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
