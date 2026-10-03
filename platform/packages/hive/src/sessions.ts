import { randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import { HIVE_LIMITS, HiveTool, parse, type HiveWake } from '@glob2/protocol';
import { Credits, HiveError, required } from './credits.ts';
type Db = Kysely<Database> | Transaction<Database>;
export interface Session {
  id: string;
  account_id: string;
  match_id: string;
  seat: number;
  team: number;
  client_id: string | null;
  lease: string | null;
  lease_until: Date | null;
  tick: string;
  generation: number;
  supervision: boolean;
  pending_run: boolean;
  run_id: string | null;
  run_until: Date | null;
  last_wake_tick: string | null;
  wake_window: Date | null;
  wake_count: number;
}
export class Sessions {
  readonly db: Kysely<Database>;
  constructor(db: Kysely<Database>) {
    this.db = db;
  }
  async authorize(account: string, match: string, seat: number) {
    const row = await this.db
      .selectFrom('match_participants as p')
      .innerJoin('matches as m', 'm.id', 'p.match_id')
      .innerJoin('accounts as a', 'a.id', 'p.account_id')
      .select(['p.team', 'a.kind', 'a.status', 'm.status as matchStatus', 'p.quit_tick'])
      .where('p.match_id', '=', match)
      .where('p.seat', '=', seat)
      .where('p.account_id', '=', account)
      .where('p.kind', '=', 'human')
      .executeTakeFirst();
    if (
      !row ||
      row.kind !== 'registered' ||
      row.status !== 'active' ||
      row.quit_tick !== null ||
      !['starting', 'running'].includes(row.matchStatus)
    )
      throw new HiveError(
        'forbidden',
        'Hive Mind is available to the player controlling an active colony.',
      );
    const entitlement = await this.db
      .selectFrom('entitlements')
      .select('id')
      .where('account_id', '=', account)
      .where('entitlement', '=', 'hive-mind')
      .where('revoked_at', 'is', null)
      .where((eb) => eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', new Date())]))
      .executeTakeFirst();
    if (!entitlement) throw new HiveError('forbidden', 'Add credits to enlist your commander.');
    return row.team;
  }
  async open(account: string, match: string, seat: number) {
    const team = await this.authorize(account, match, seat);
    return required(
      (
        await sql<Session>`INSERT INTO hive_sessions(account_id,match_id,seat,team) VALUES(${account},${match},${seat},${team})
   ON CONFLICT(account_id,match_id,seat) DO UPDATE SET team=EXCLUDED.team RETURNING *`.execute(
          this.db,
        )
      ).rows[0],
    );
  }
  async get(id: string, db: Db = this.db, lock = false) {
    const row = (
      await (
        lock
          ? sql<Session>`SELECT * FROM hive_sessions WHERE id=${id} FOR UPDATE`
          : sql<Session>`SELECT * FROM hive_sessions WHERE id=${id}`
      ).execute(db)
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'Commander not found.');
    return row;
  }
  async event(id: string, dedup: string, kind: string, body: unknown, db: Db = this.db) {
    return (
      (
        await sql`INSERT INTO hive_events(session_id,dedup,kind,body) VALUES(${id},${dedup},${kind},${JSON.stringify(body)}::jsonb)
    ON CONFLICT(session_id,dedup) DO NOTHING RETURNING id`.execute(db)
      ).rows.length === 1
    );
  }
  async report(id: string, text: string, db: Db = this.db) {
    await this.event(id, randomUUID(), 'report', { text }, db);
  }
  async poll(
    id: string,
    client: string,
    lease: string | undefined,
    tick: number,
    caughtUp: boolean,
  ) {
    return this.db.transaction().execute(async (db) => {
      let session = await this.get(id, db, true);
      const now = Date.now();
      if (session.run_until && +session.run_until < now) {
        await sql`UPDATE hive_sessions SET pending_run=true,run_until=NULL,run_id=NULL WHERE id=${id}`.execute(
          db,
        );
        await sql`UPDATE hive_operations SET status=CASE WHEN status='pending' THEN 'cancelled' ELSE 'uncertain' END WHERE session_id=${id} AND status IN ('pending','dispatched')`.execute(
          db,
        );
        await this.report(
          id,
          'I was interrupted. I will reassess the colony before continuing.',
          db,
        );
      }
      if (session.lease_until && +session.lease_until > now && session.client_id !== client)
        throw new HiveError('conflict', 'Your commander is connected in another window.');
      if (
        session.client_id === client &&
        session.lease_until &&
        +session.lease_until > now &&
        lease !== session.lease
      )
        throw new HiveError('conflict', 'Reconnect your commander.');
      const renewed =
        session.client_id !== client || !session.lease_until || +session.lease_until <= now;
      const nextLease = renewed ? randomUUID() : required(session.lease);
      if (renewed) {
        const uncertain = (
          await sql`UPDATE hive_operations SET status='uncertain' WHERE session_id=${id} AND status='dispatched' RETURNING id`.execute(
            db,
          )
        ).rows;
        if (uncertain.length) {
          await sql`UPDATE hive_programs SET status='paused' WHERE session_id=${id} AND status='active'`.execute(
            db,
          );
          await this.report(
            id,
            'Some standing orders need review after reconnecting. They are paused to avoid repeating an action.',
            db,
          );
        }
      }
      await sql`UPDATE hive_sessions SET client_id=${client},lease=${nextLease},lease_until=now()+interval '15 seconds',
     tick=GREATEST(tick,${tick}) WHERE id=${id}`.execute(db);
      session = { ...session, lease: nextLease };
      const operations =
        caughtUp && tick >= Number(session.tick)
          ? (
              await sql<{
                id: string;
                request: unknown;
              }>`UPDATE hive_operations SET status='dispatched',lease=${nextLease}
    WHERE id IN (SELECT id FROM hive_operations WHERE session_id=${id} AND status='pending' ORDER BY created_at,id LIMIT 1) RETURNING id,request`.execute(
                db,
              )
            ).rows
          : [];
      const programs = (
        await sql<{
          definition: unknown;
          status: string;
        }>`SELECT definition,status FROM hive_programs WHERE session_id=${id} AND status<>'removed' ORDER BY id`.execute(
          db,
        )
      ).rows;
      return { lease: nextLease, team: session.team, operations, programs };
    });
  }
  async command(id: string, commandId: string, text: string, ongoing: boolean) {
    return this.db.transaction().execute(async (db) => {
      await this.get(id, db, true);
      if (!(await this.event(id, `command:${commandId}`, 'command', { text, ongoing }, db)))
        return false;
      await sql`UPDATE hive_sessions SET generation=generation+1,supervision=${ongoing},pending_run=true WHERE id=${id}`.execute(
        db,
      );
      await sql`UPDATE hive_operations SET status='cancelled' WHERE session_id=${id} AND status='pending'`.execute(
        db,
      );
      return true;
    });
  }
  async stop(id: string) {
    await this.db.transaction().execute(async (db) => {
      await this.get(id, db, true);
      await sql`UPDATE hive_programs SET supervised=false WHERE session_id=${id}`.execute(db);
      await sql`UPDATE hive_operations SET supervised=false WHERE session_id=${id}`.execute(db);
      await sql`UPDATE hive_sessions SET generation=generation+1,supervision=false,pending_run=false WHERE id=${id}`.execute(
        db,
      );
      await sql`UPDATE hive_operations SET status='cancelled' WHERE session_id=${id} AND status='pending'`.execute(
        db,
      );
      await this.report(id, 'I have stopped working. Your standing orders remain active.', db);
    });
  }
  async enqueue(id: string, generation: number, request: unknown) {
    const tool = parse(HiveTool, request);
    const op = randomUUID();
    if (
      ('source' in tool && Buffer.byteLength(tool.source) > HIVE_LIMITS.sourceBytes) ||
      ('program' in tool && Buffer.byteLength(tool.program.source) > HIVE_LIMITS.sourceBytes)
    )
      throw new HiveError('bad_request', 'Standing order is too large.');
    await this.db.transaction().execute(async (db) => {
      const session = await this.get(id, db, true);
      if (session.generation !== generation)
        throw new HiveError('cancelled', 'The command has changed.');
      if (!session.lease_until || +session.lease_until < Date.now())
        throw new HiveError('disconnected', 'Waiting for your colony to reconnect.');
      if (tool.kind !== 'execute' && tool.kind !== 'list') {
        const programId = 'program' in tool ? tool.program.id : tool.programId;
        const current = (
          await sql<{
            revision: number;
            status: string;
          }>`SELECT revision,status FROM hive_programs WHERE session_id=${id} AND id=${programId}`.execute(
            db,
          )
        ).rows[0];
        if (tool.kind === 'install') {
          const count = (
            await sql<{
              count: string;
            }>`SELECT COUNT(*)::text AS count FROM hive_programs WHERE session_id=${id} AND status<>'removed'`.execute(
              db,
            )
          ).rows[0];
          if (
            current ||
            tool.program.revision !== 1 ||
            Number(count?.count) >= HIVE_LIMITS.programs
          )
            throw new HiveError('conflict', 'Cannot install that standing order.');
        } else if (
          !current ||
          current.status === 'removed' ||
          current.revision !== tool.expectedRevision ||
          ('program' in tool && tool.program.revision !== current.revision + 1)
        )
          throw new HiveError(
            'conflict',
            'That standing order changed. Inspect it before editing.',
          );
      }
      const queued = (
        await sql<{
          count: string;
        }>`SELECT COUNT(*)::text AS count FROM hive_operations WHERE session_id=${id} AND status IN ('pending','dispatched')`.execute(
          db,
        )
      ).rows[0];
      if (Number(queued?.count) >= 16)
        throw new HiveError('conflict', 'Wait for the current colony orders to finish.');
      await sql`INSERT INTO hive_operations(id,session_id,generation,status,request,supervised) VALUES(${op},${id},${generation},'pending',${JSON.stringify(tool)}::jsonb,${session.supervision})`.execute(
        db,
      );
    });
    return op;
  }
  async result(
    id: string,
    operation: string,
    lease: string,
    status: string,
    output: string,
    tick: number,
  ) {
    if (Buffer.byteLength(output) > HIVE_LIMITS.outputBytes)
      throw new HiveError('bad_request', 'Report is too large.');
    return this.db.transaction().execute(async (db) => {
      const s = await this.get(id, db, true);
      if (s.lease !== lease || !s.lease_until || +s.lease_until < Date.now())
        throw new HiveError('conflict', 'The commander connection has expired.');
      const op = (
        await sql<{
          status: string;
          request: unknown;
          result: unknown;
          supervised: boolean;
          lease: string;
        }>`SELECT * FROM hive_operations WHERE id=${operation} AND session_id=${id} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!op || op.lease !== lease) throw new HiveError('not_found', 'Unknown command result.');
      if (op.status !== 'dispatched') {
        if (
          op.status === status &&
          JSON.stringify(op.result) !== JSON.stringify({ output, tick })
        ) {
          const previous = op.result as { output?: string; tick?: number };
          if (previous?.output !== output || previous?.tick !== tick)
            throw new HiveError('conflict', 'Command result changed.');
        }
        return op.status === status;
      }
      await sql`UPDATE hive_operations SET status=${status},result=${JSON.stringify({ output, tick })}::jsonb WHERE id=${operation}`.execute(
        db,
      );
      if (status === 'completed') {
        const tool = parse(HiveTool, op.request);
        if (tool.kind === 'install' || tool.kind === 'replace') {
          const p = tool.program;
          await sql`INSERT INTO hive_programs(session_id,id,revision,definition,status,supervised) VALUES(${id},${p.id},${p.revision},${JSON.stringify(p)}::jsonb,'active',${op.supervised})
      ON CONFLICT(session_id,id) DO UPDATE SET revision=EXCLUDED.revision,definition=EXCLUDED.definition,status='active',supervised=hive_programs.supervised OR EXCLUDED.supervised`.execute(
            db,
          );
        } else if (tool.kind === 'pause' || tool.kind === 'resume' || tool.kind === 'remove') {
          const state =
            tool.kind === 'resume' ? 'active' : tool.kind === 'pause' ? 'paused' : 'removed';
          await sql`UPDATE hive_programs SET status=${state} WHERE session_id=${id} AND id=${tool.programId} AND revision=${tool.expectedRevision}`.execute(
            db,
          );
        }
      }
      return true;
    });
  }
  async wake(id: string, wake: HiveWake, lease: string) {
    return this.db.transaction().execute(async (db) => {
      const s = await this.get(id, db, true);
      if (s.lease !== lease || !s.lease_until || +s.lease_until < Date.now())
        throw new HiveError('conflict', 'Commander connection expired.');
      const p = (
        await sql<{
          supervised: boolean;
        }>`SELECT supervised FROM hive_programs WHERE session_id=${id} AND id=${wake.programId} AND revision=${wake.revision} AND status='active'`.execute(
          db,
        )
      ).rows[0];
      if (!p || wake.tick > Number(s.tick) || wake.tick < Number(s.last_wake_tick ?? 0))
        return false;
      // A delivery retry must not create another report or consume the wake budget,
      // even if it arrives with a later tick or a different coalescing key.
      if (!(await this.event(id, `wake-receipt:${wake.eventId}`, 'wake_receipt', {}, db)))
        return false;
      if (
        !(await this.event(
          id,
          `wake:${wake.programId}:${wake.revision}:${wake.key}:${Math.floor(wake.tick / 25)}`,
          'wake',
          wake,
          db,
        ))
      )
        return false;
      await this.report(id, wake.reason, db);
      if (!p.supervised || (s.last_wake_tick !== null && wake.tick - Number(s.last_wake_tick) < 25))
        return false;
      const count = s.wake_window && Date.now() - +s.wake_window < 60000 ? s.wake_count : 0;
      if (count >= HIVE_LIMITS.wakesPerMinute) return false;
      // Credit check is advisory here; the runner reserves transactionally before every call.
      const balance = await new Credits(this.db).balance(s.account_id, db);
      if (balance.available <= 0) return false;
      await this.event(id, `accepted:${wake.eventId}`, 'trigger', wake, db);
      await sql`UPDATE hive_sessions SET pending_run=true,last_wake_tick=${wake.tick},wake_count=${count + 1},
    wake_window=CASE WHEN ${count}=0 THEN now() ELSE wake_window END WHERE id=${id}`.execute(db);
      return true;
    });
  }
  async takeTriggers(id: string, generation: number) {
    return this.db.transaction().execute(async (db) => {
      const s = await this.get(id, db, true);
      if (s.generation !== generation) return [];
      const events = (
        await sql<{
          body: unknown;
          created_at: Date;
        }>`UPDATE hive_events SET kind='consumed_trigger' WHERE session_id=${id} AND kind='trigger' RETURNING body,created_at`.execute(
          db,
        )
      ).rows;
      if (events.length)
        await sql`UPDATE hive_sessions SET pending_run=false WHERE id=${id}`.execute(db);
      // Never replay stale alerts after a top-up or a long disconnection.
      const authorized = (
        await sql<{
          id: string;
          revision: number;
        }>`SELECT id,revision FROM hive_programs WHERE session_id=${id} AND supervised=true AND status='active'`.execute(
          db,
        )
      ).rows;
      return events
        .filter((e) => {
          const wake = e.body as HiveWake;
          return (
            Date.now() - +e.created_at < 30000 &&
            authorized.some((p) => p.id === wake.programId && p.revision === wake.revision)
          );
        })
        .slice(-8)
        .map((e) => e.body);
    });
  }
  async events(id: string, after: string = '0') {
    return (
      await sql<{
        id: string;
        kind: string;
        body: unknown;
      }>`SELECT id,kind,body FROM hive_events WHERE session_id=${id} AND id>${after}::bigint
   AND kind IN ('command','report','progress') ORDER BY id LIMIT 100`.execute(this.db)
    ).rows;
  }
}
