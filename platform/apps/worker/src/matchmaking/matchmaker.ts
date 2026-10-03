// The quick-match matchmaker. One tick (every second, on the worker replica
// holding the scheduler leader lock):
//   1. resolve accept prompts: all accepted → starting; a decline or the
//      deadline → decliners leave the queue with a cooldown, the rest wait
//      again at their original position;
//   2. form groups from waiting tickets (grouping.ts) and record proposals;
//   3. hand starting proposals to the MatchStarter;
//   4. send queue.status progress to waiting players.
// All state lives in Postgres, so a new leader continues where the old one
// stopped; every step re-checks state under row locks, and the API's ticket
// operations (tickets.ts) may run concurrently.
//
// Starts run in the background, one per proposal: a start can wait up to a
// minute for on-demand map generation, and must not hold up proposals,
// grouping or status for every other queue. A tick waits briefly
// (startWaitMs) so fast starts are reported in its summary; slower ones are
// counted by the tick in which they finish. With `fence`, every leader-only
// transaction first checks the scheduler lease (assertLease), so a replaced
// leader cannot commit proposals or starts.
import { randomInt, randomUUID } from 'node:crypto';
import { sql, type Kysely, type Selectable, type Transaction } from 'kysely';
import type { Logger, MapPoolEntry, ResolvedQueue } from '@glob2/core';
import type { Database } from '@glob2/db';
import { systemClock, type Clock } from '../clock.ts';
import { aiLadderRatings, ensureAccountEntity, type RatedAi } from '../ratings/entities.ts';
import { DEFAULT_RATING, displayRating, isProvisional, matchSkill } from '../ratings/scale.ts';
import {
  backfillAt,
  backfillDue,
  planGroups,
  ratingWindow,
  waitedSeconds,
  type PlannedGroup,
  type RegionRtt,
  type WaitingTicket,
} from './grouping.ts';
import type { QueueNotifier } from './notifier.ts';
import { sendProposal } from './proposalView.ts';
import type { MatchProposal, MatchStarter, ProposalSeat } from './starter.ts';

type Db = Kysely<Database>;

export interface MatchmakerOptions {
  db: Db;
  queues: readonly ResolvedQueue[];
  starter: MatchStarter;
  notifier: QueueNotifier;
  clock?: Clock;
  /** Uniform integer in [0, n): picks the map pool entry. */
  random?: (n: number) => number;
  logger?: Pick<Logger, 'info' | 'warn' | 'error'>;
  /** Seconds between queue.status events per ticket (default 5). */
  statusIntervalSeconds?: number;
  /** Start attempts before a proposal fails and its players wait again (default 3). */
  maxStartAttempts?: number;
  /** How long a tick waits for the starts it launched (default 250 ms). */
  startWaitMs?: number;
  /** Starts in flight at once (default 8); further proposals wait for a slot. */
  maxConcurrentStarts?: number;
  /** Leader fencing: called first in every matchmaker transaction; throws when no longer leader. */
  fence?: (trx: Transaction<Database>) => Promise<void>;
}

export interface TickSummary {
  proposed: number;
  started: number;
  cancelled: number;
  failed: number;
}

class Skip extends Error {}

export class Matchmaker {
  private readonly db: Db;
  private readonly queues: Map<string, ResolvedQueue>;
  private readonly starter: MatchStarter;
  private readonly notifier: QueueNotifier;
  private readonly clock: Clock;
  private readonly random: (n: number) => number;
  private readonly logger: MatchmakerOptions['logger'];
  private readonly statusInterval: number;
  private readonly maxStartAttempts: number;
  private readonly startWaitMs: number;
  private readonly maxConcurrentStarts: number;
  private readonly fence: (trx: Transaction<Database>) => Promise<void>;
  private readonly lastStatus = new Map<string, number>();
  /** Starts in flight, by proposal id. */
  private readonly starting = new Map<string, Promise<void>>();
  /** Outcomes of starts that finished since the last tick summary. */
  private finished = { started: 0, failed: 0 };

  constructor(options: MatchmakerOptions) {
    this.db = options.db;
    this.queues = new Map(options.queues.map((q) => [q.id, q]));
    this.starter = options.starter;
    this.notifier = options.notifier;
    this.clock = options.clock ?? systemClock;
    this.random = options.random ?? ((n) => randomInt(n));
    this.logger = options.logger;
    this.statusInterval = options.statusIntervalSeconds ?? 5;
    this.maxStartAttempts = options.maxStartAttempts ?? 3;
    this.startWaitMs = options.startWaitMs ?? 250;
    this.maxConcurrentStarts = options.maxConcurrentStarts ?? 8;
    this.fence = options.fence ?? (async () => undefined);
  }

  async tick(): Promise<TickSummary> {
    const summary: TickSummary = { proposed: 0, started: 0, cancelled: 0, failed: 0 };
    summary.cancelled = await this.resolvePending();
    for (const queue of this.queues.values()) summary.proposed += await this.formGroups(queue);
    await this.launchStarts();
    await this.waitForStarts(this.startWaitMs);
    summary.started = this.finished.started;
    summary.failed = this.finished.failed;
    this.finished = { started: 0, failed: 0 };
    await this.sendStatus();
    return summary;
  }

  /** Proposals whose start is running in the background. */
  get startsInFlight(): number {
    return this.starting.size;
  }

  /** Waits until every start in flight has finished (or `timeoutMs` passed). */
  async waitForStarts(timeoutMs = Number.POSITIVE_INFINITY): Promise<void> {
    if (this.starting.size === 0 || timeoutMs <= 0) return;
    let timer: NodeJS.Timeout | undefined;
    const all = Promise.allSettled([...this.starting.values()]);
    await (Number.isFinite(timeoutMs)
      ? Promise.race([all, new Promise((resolve) => (timer = setTimeout(resolve, timeoutMs)))])
      : all);
    clearTimeout(timer);
  }

  // ------------------------------------------------------------ accepts

  private async resolvePending(): Promise<number> {
    const now = this.clock.now();
    const pending = await this.db
      .selectFrom('match_proposals')
      .select('id')
      .where('status', '=', 'pending')
      .orderBy('created_at')
      .execute();
    let cancelled = 0;
    for (const { id } of pending) {
      const outcome = await this.db.transaction().execute(async (trx) => {
        await this.fence(trx);
        const proposal = await trx
          .selectFrom('match_proposals')
          .selectAll()
          .where('id', '=', id)
          .where('status', '=', 'pending')
          .forUpdate()
          .executeTakeFirst();
        if (!proposal) return 'gone';
        const seats = (
          await trx
            .selectFrom('match_proposal_seats')
            .select(['slot', 'ticket_id', 'account_id', 'response'])
            .where('proposal_id', '=', id)
            .where('kind', '=', 'human')
            .execute()
        ).flatMap((s) =>
          s.ticket_id && s.account_id
            ? [
                {
                  slot: s.slot,
                  ticketId: s.ticket_id,
                  accountId: s.account_id,
                  response: s.response,
                },
              ]
            : [],
        );
        const declined = seats.filter((s) => s.response === 'declined');
        const waiting = seats.filter((s) => s.response === 'pending');
        const expired = proposal.expires_at !== null && now >= proposal.expires_at;
        if (declined.length === 0 && waiting.length === 0) {
          await trx
            .updateTable('match_proposals')
            .set({ status: 'starting' })
            .where('id', '=', id)
            .execute();
          return 'accepted';
        }
        if (declined.length === 0 && !expired) return 'waiting';

        const queue = this.queues.get(proposal.queue_id);
        const cooldownSeconds = queue?.declineCooldownSeconds ?? 60;
        const until = new Date(now.getTime() + cooldownSeconds * 1000);
        const timedOut = expired && declined.length === 0 ? waiting : [];
        if (timedOut.length > 0) {
          await trx
            .updateTable('match_proposal_seats')
            .set({ response: 'timeout', responded_at: now })
            .where('proposal_id', '=', id)
            .where('response', '=', 'pending')
            .execute();
        }
        const removed = [
          ...declined.map((s) => ({ seat: s, reason: 'declined' as const })),
          ...timedOut.map((s) => ({ seat: s, reason: 'timeout' as const })),
        ];
        const removedSlots = new Set(removed.map((r) => r.seat.slot));
        for (const { seat, reason } of removed) {
          await trx
            .updateTable('queue_tickets')
            .set({ status: 'declined', updated_at: now })
            .where('id', '=', seat.ticketId)
            .where('status', '=', 'proposed')
            .execute();
          if (cooldownSeconds > 0) {
            await trx
              .insertInto('queue_cooldowns')
              .values({ account_id: seat.accountId, until, reason, created_at: now })
              .onConflict((oc) =>
                oc.column('account_id').doUpdateSet({ until, reason, created_at: now }),
              )
              .execute();
          }
          await this.notifier.send(trx, seat.accountId, 'queue.proposalEnded', {
            proposalId: id,
            ticketId: seat.ticketId,
            outcome: 'removed',
            reason,
            ...(cooldownSeconds > 0 ? { cooldownUntil: until.toISOString() } : {}),
          });
        }
        for (const seat of seats.filter((s) => !removedSlots.has(s.slot))) {
          await this.requeue(trx, id, seat.ticketId, seat.accountId, 'other_declined', now);
        }
        await trx
          .updateTable('match_proposals')
          .set({ status: 'cancelled', resolved_at: now })
          .where('id', '=', id)
          .execute();
        return 'cancelled';
      });
      if (outcome === 'cancelled') cancelled++;
    }
    return cancelled;
  }

  /** Puts a ticket back in its queue at its original position (created_at is kept). */
  private async requeue(
    trx: Transaction<Database>,
    proposalId: string,
    ticketId: string,
    accountId: string,
    reason: 'other_declined' | 'start_failed',
    now: Date,
  ): Promise<void> {
    const updated = await trx
      .updateTable('queue_tickets')
      .set({ status: 'waiting', proposal_id: null, updated_at: now })
      .where('id', '=', ticketId)
      .where('status', '=', 'proposed')
      .executeTakeFirst();
    if (updated.numUpdatedRows === 0n) return; // the player left meanwhile
    await this.notifier.send(trx, accountId, 'queue.proposalEnded', {
      proposalId,
      ticketId,
      outcome: 'requeued',
      reason,
    });
  }

  // ------------------------------------------------------------ grouping

  private async formGroups(queue: ResolvedQueue): Promise<number> {
    const now = this.clock.now();
    const rows = await this.db
      .selectFrom('queue_tickets')
      .select([
        'id',
        'account_id',
        'sim_version',
        'created_at',
        'rating_mu',
        'rating_sigma',
        'region_rtts',
        'allow_ai_opponent',
      ])
      .where('queue_id', '=', queue.id)
      .where('status', '=', 'waiting')
      .orderBy('created_at')
      .orderBy('id')
      .execute();
    const byVersion = new Map<string, WaitingTicket[]>();
    for (const row of rows) {
      const list = byVersion.get(row.sim_version) ?? [];
      list.push(toTicket(row));
      byVersion.set(row.sim_version, list);
    }
    let proposed = 0;
    for (const [simVersion, tickets] of byVersion) {
      const needAis = tickets.some((t) => backfillDue(queue, t, now));
      const ais = needAis
        ? (await aiLadderRatings(this.db, queue.aiPool, simVersion, queue.id)).map((r) => ({
            ai: r.ai,
            entityId: r.entityId,
            mu: r.mu,
            sigma: r.sigma,
          }))
        : [];
      for (const group of planGroups(queue, tickets, ais, now)) {
        if (await this.propose(queue, simVersion, group, now)) proposed++;
      }
    }
    return proposed;
  }

  private async propose(
    queue: ResolvedQueue,
    simVersion: string,
    group: PlannedGroup,
    now: Date,
  ): Promise<boolean> {
    const id = randomUUID();
    const requiresAccept = queue.acceptSeconds > 0 && !group.backfilled;
    const map = queue.mapPool[this.random(queue.mapPool.length)];
    if (!map) throw new Error(`queue ${queue.id} has an empty map pool`);
    const members = group.sides.flatMap((side, sideIndex) =>
      side.map((member) => ({ member, side: sideIndex })),
    );
    const humans = members.flatMap(({ member }) =>
      member.kind === 'human' ? [member.ticket] : [],
    );
    const expiresAt = requiresAccept ? new Date(now.getTime() + queue.acceptSeconds * 1000) : null;
    try {
      await this.db.transaction().execute(async (trx) => {
        await this.fence(trx);
        await trx
          .insertInto('match_proposals')
          .values({
            id,
            queue_id: queue.id,
            sim_version: simVersion,
            region: group.region,
            rated: queue.rated,
            backfilled: group.backfilled,
            status: requiresAccept ? 'pending' : 'starting',
            map: JSON.stringify(map),
            expires_at: expiresAt,
            created_at: now,
          })
          .execute();
        const claimed = await trx
          .updateTable('queue_tickets')
          .set({ status: 'proposed', proposal_id: id, updated_at: now })
          .where(
            'id',
            'in',
            humans.map((t) => t.id),
          )
          .where('status', '=', 'waiting')
          .returning('id')
          .execute();
        if (claimed.length !== humans.length) throw new Skip(); // someone left meanwhile
        for (const [slot, { member, side }] of members.entries()) {
          const human = member.kind === 'human';
          await trx
            .insertInto('match_proposal_seats')
            .values({
              proposal_id: id,
              slot,
              side,
              kind: member.kind,
              ticket_id: human ? member.ticket.id : null,
              account_id: human ? member.ticket.accountId : null,
              ai_id: human ? null : member.candidate.ai,
              rating_entity_id: human
                ? await ensureAccountEntity(trx, member.ticket.accountId)
                : member.candidate.entityId,
              mu: human ? member.ticket.mu : member.candidate.mu,
              sigma: human ? member.ticket.sigma : member.candidate.sigma,
              response: human && requiresAccept ? 'pending' : 'not_required',
            })
            .execute();
        }
        await sendProposal(trx, this.notifier, id);
      });
    } catch (error) {
      if (error instanceof Skip) return false;
      throw error;
    }
    this.logger?.info(
      { proposal: id, queue: queue.id, humans: humans.length, region: group.region },
      'match proposed',
    );
    return true;
  }

  // ------------------------------------------------------------ starting

  /** Starts every proposal in 'starting' that is not already being started, in the background. */
  private async launchStarts(): Promise<void> {
    const free = this.maxConcurrentStarts - this.starting.size;
    if (free <= 0) return;
    let query = this.db
      .selectFrom('match_proposals')
      .selectAll()
      .where('status', '=', 'starting')
      .orderBy('created_at')
      .limit(free);
    if (this.starting.size > 0) query = query.where('id', 'not in', [...this.starting.keys()]);
    const ready = await query.execute();
    for (const row of ready) {
      const work = this.startOne(row)
        .then((outcome) => {
          if (outcome === 'started') this.finished.started++;
          else if (outcome === 'failed') this.finished.failed++;
        })
        .catch((error: unknown) =>
          this.logger?.error({ err: error, proposal: row.id }, 'match start bookkeeping failed'),
        )
        .finally(() => this.starting.delete(row.id));
      this.starting.set(row.id, work);
    }
  }

  /** One proposal's start: 'started', 'failed' (gave up), or 'retry' (a later tick tries again). */
  private async startOne(
    row: Selectable<Database['match_proposals']>,
  ): Promise<'started' | 'failed' | 'retry'> {
    const seats = await this.db
      .selectFrom('match_proposal_seats')
      .selectAll()
      .where('proposal_id', '=', row.id)
      .orderBy('slot')
      .execute();
    const proposal: MatchProposal = {
      id: row.id,
      queueId: row.queue_id,
      rated: row.rated,
      backfilled: row.backfilled,
      simVersion: row.sim_version,
      region: row.region,
      map: row.map as unknown as MapPoolEntry,
      seats: seats.map((s): ProposalSeat => ({
        slot: s.slot,
        side: s.side,
        kind: s.kind,
        ...(s.ticket_id ? { ticketId: s.ticket_id } : {}),
        ...(s.account_id ? { accountId: s.account_id } : {}),
        ...(s.ai_id ? { ai: s.ai_id as RatedAi } : {}),
        ratingEntityId: s.rating_entity_id,
        mu: s.mu,
        sigma: s.sigma,
      })),
    };
    let matchId: string;
    try {
      ({ matchId } = await this.starter.start(proposal));
    } catch (error) {
      const now = this.clock.now();
      const gaveUp = await this.db.transaction().execute(async (trx) => {
        await this.fence(trx);
        const current = await trx
          .updateTable('match_proposals')
          .set((eb) => ({ start_attempts: eb('start_attempts', '+', 1) }))
          .where('id', '=', row.id)
          .where('status', '=', 'starting')
          .returning('start_attempts')
          .executeTakeFirst();
        if (!current || current.start_attempts < this.maxStartAttempts) return false;
        await trx
          .updateTable('match_proposals')
          .set({ status: 'failed', failure: String(error), resolved_at: now })
          .where('id', '=', row.id)
          .execute();
        for (const seat of proposal.seats) {
          if (!seat.ticketId || !seat.accountId) continue;
          await this.requeue(trx, row.id, seat.ticketId, seat.accountId, 'start_failed', now);
        }
        return true;
      });
      this.logger?.warn({ err: error, proposal: row.id, gaveUp }, 'match start failed');
      return gaveUp ? 'failed' : 'retry';
    }
    const now = this.clock.now();
    const recorded = await this.db.transaction().execute(async (trx) => {
      await this.fence(trx);
      const updated = await trx
        .updateTable('match_proposals')
        .set({ status: 'started', match_id: matchId, resolved_at: now })
        .where('id', '=', row.id)
        .where('status', '=', 'starting')
        .executeTakeFirst();
      if (updated.numUpdatedRows === 0n) return false;
      for (const seat of proposal.seats) {
        if (!seat.ticketId || !seat.accountId) continue;
        await trx
          .updateTable('queue_tickets')
          .set({ status: 'matched', match_id: matchId, updated_at: now })
          .where('id', '=', seat.ticketId)
          .execute();
        await this.notifier.send(trx, seat.accountId, 'queue.matchFound', {
          ticketId: seat.ticketId,
          matchId,
        });
      }
      return true;
    });
    if (!recorded) return 'retry';
    this.logger?.info({ proposal: row.id, match: matchId }, 'match started');
    return 'started';
  }

  // ------------------------------------------------------------ progress

  private async sendStatus(): Promise<void> {
    const now = this.clock.now();
    const rows = await this.db
      .selectFrom('queue_tickets')
      .select([
        'id',
        'queue_id',
        'account_id',
        'sim_version',
        'created_at',
        'rating_mu',
        'rating_sigma',
        'region_rtts',
        'allow_ai_opponent',
      ])
      .where('status', '=', 'waiting')
      .execute();
    const live = new Set<string>();
    // Per call: the AI ladder of each queue and sim version, and each queue's typical wait.
    const aiLadders = new Map<string, Promise<{ ai: RatedAi; mu: number; sigma: number }[]>>();
    const typicalWaits = new Map<string, Promise<number | undefined>>();
    for (const row of rows) {
      live.add(row.id);
      const queue = this.queues.get(row.queue_id);
      if (!queue) continue;
      const last = this.lastStatus.get(row.id);
      if (last !== undefined && now.getTime() - last < this.statusInterval * 1000) continue;
      this.lastStatus.set(row.id, now.getTime());
      const ticket = toTicket(row);
      const waited = waitedSeconds(ticket, now);
      const window = ratingWindow(queue, waited);
      const at = backfillAt(queue, ticket);
      const skill = matchSkill(ticket);
      const best = [...ticket.regions].sort((a, b) => a.rttMs - b.rttMs)[0];
      let backfillAi: { ai: RatedAi; rating: number } | undefined;
      if (at && queue.aiPool.length > 0) {
        const key = `${queue.id}\n${row.sim_version}`;
        let pending = aiLadders.get(key);
        if (!pending) {
          pending = aiLadderRatings(this.db, queue.aiPool, row.sim_version, queue.id);
          aiLadders.set(key, pending);
        }
        const ladder = await pending;
        const closest = [...ladder].sort(
          (a, b) =>
            Math.abs(matchSkill(a) - skill) - Math.abs(matchSkill(b) - skill) ||
            a.ai.localeCompare(b.ai),
        )[0];
        if (closest) backfillAi = { ai: closest.ai, rating: Math.round(displayRating(closest)) };
      }
      let wait = typicalWaits.get(queue.id);
      if (!wait) {
        wait = this.typicalWait(queue.id, now);
        typicalWaits.set(queue.id, wait);
      }
      const typical = await wait;
      const rating = { mu: ticket.mu, sigma: ticket.sigma };
      await this.notifier.send(this.db, row.account_id, 'queue.status', {
        ticketId: row.id,
        queueId: row.queue_id,
        waitedSeconds: Math.floor(waited),
        ratingWindow: Math.round(window),
        ...(at ? { aiBackfillAt: at.toISOString() } : {}),
        ratingRange: { min: Math.round(skill - window), max: Math.round(skill + window) },
        rating: Math.round(displayRating(rating)),
        provisional: isProvisional(rating),
        ...(best ? { region: best.region, rttMs: best.rttMs } : {}),
        allowAiOpponent: ticket.allowAi,
        ...(backfillAi ? { backfillAi } : {}),
        ...(typical !== undefined ? { typicalWaitSeconds: typical } : {}),
      });
    }
    for (const id of this.lastStatus.keys()) if (!live.has(id)) this.lastStatus.delete(id);
  }

  /** Median wait (seconds) of the queue's tickets matched in the last day; undefined with none. */
  private async typicalWait(queueId: string, now: Date): Promise<number | undefined> {
    const since = new Date(now.getTime() - 24 * 3600 * 1000);
    const row = await this.db
      .selectFrom('queue_tickets')
      .select(
        sql<
          number | null
        >`percentile_cont(0.5) WITHIN GROUP (ORDER BY extract(epoch FROM updated_at - created_at))`.as(
          'median',
        ),
      )
      .where('queue_id', '=', queueId)
      .where('status', '=', 'matched')
      .where('updated_at', '>=', since)
      .executeTakeFirst();
    const median = row?.median;
    return median === null || median === undefined ? undefined : Math.max(0, Math.round(median));
  }
}

function toTicket(row: {
  id: string;
  account_id: string;
  created_at: Date;
  rating_mu: number | null;
  rating_sigma: number | null;
  region_rtts: unknown;
  allow_ai_opponent: boolean;
}): WaitingTicket {
  return {
    id: row.id,
    accountId: row.account_id,
    createdAt: row.created_at,
    mu: row.rating_mu ?? DEFAULT_RATING.mu,
    sigma: row.rating_sigma ?? DEFAULT_RATING.sigma,
    regions: Array.isArray(row.region_rtts) ? (row.region_rtts as RegionRtt[]) : [],
    allowAi: row.allow_ai_opponent,
  };
}
