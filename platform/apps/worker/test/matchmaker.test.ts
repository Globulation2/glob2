import { afterAll, afterEach, beforeAll, describe, expect, it } from 'vitest';
import { sql } from 'kysely';
import { createLogger, resolveQueue, type ResolvedQueue } from '@glob2/core';
import { LeaderElection, LeaderLostError } from '@glob2/db';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { checkDocument } from '@glob2/protocol';
import {
  DISPLAY_PER_MU,
  FakeClock,
  MU0,
  aiSeedRating,
  balanceSides,
  chooseRegion,
  joinQueue,
  leaveQueue,
  planGroups,
  ratingWindow,
  respondToProposal,
  rttTolerance,
  sendProposal,
  updateTicket,
  type WaitingTicket,
  type MatchProposal,
} from '@glob2/play';
import {
  InMemoryMatchStarter,
  RecordingQueueNotifier,
  SIM_A,
  SIM_B,
  createAccount,
  waitFor,
} from '@glob2/play/testing';
import { Matchmaker } from '../src/matchmaking/matchmaker.ts';
import { runScheduler } from '../src/scheduler.ts';

const RANKED: ResolvedQueue = resolveQueue({
  id: 'ranked-1v1',
  name: 'Ranked 1v1',
  mode: '1v1',
  rated: true,
  aiBackfillSeconds: 90,
});
const RANKED_2V2 = resolveQueue({
  id: 'ranked-2v2',
  name: 'Ranked 2v2',
  mode: '2v2',
  rated: true,
  aiBackfillSeconds: 120,
});
const CASUAL = resolveQueue({
  id: 'casual-1v1',
  name: 'Casual',
  mode: '1v1',
  rated: false,
  aiBackfillSeconds: 30,
});

/** μ whose matchmaking skill is `skill` display points. */
const muFor = (skill: number) => MU0 + (skill - 1500) / DISPLAY_PER_MU;

let database: TestDatabase;
// Registered display names are unique (identity migration 0003).
let players = 0;
beforeAll(async () => {
  database = await createTestDatabase({ role: 'worker' });
});
afterAll(async () => {
  await database?.drop();
});
afterEach(async () => {
  // Each test starts with empty queues (TRUNCATE is the schema owner's).
  await sql`TRUNCATE queue_tickets, match_proposals, match_proposal_seats, queue_cooldowns, matches CASCADE`.execute(
    database.as('migrator').db,
  );
});

interface Harness {
  clock: FakeClock;
  notifier: RecordingQueueNotifier;
  starter: InMemoryMatchStarter;
  matchmaker: Matchmaker;
}

function harness(
  queues: ResolvedQueue[] = [RANKED, RANKED_2V2, CASUAL],
  clock = new FakeClock(),
): Harness {
  const notifier = new RecordingQueueNotifier();
  const starter = new InMemoryMatchStarter(database.db);
  const matchmaker = new Matchmaker({
    db: database.db,
    queues,
    starter,
    notifier,
    clock,
    random: () => 0,
    // Starts run in the background; these tests count them in the tick that launched them.
    startWaitMs: 60_000,
  });
  return { clock, notifier, starter, matchmaker };
}

/** A starter whose starts wait until released (a slow on-demand map generation). */
class GatedStarter extends InMemoryMatchStarter {
  private release: () => void = () => undefined;
  private gate = new Promise<void>((resolve) => (this.release = resolve));
  readonly entered: string[] = [];

  override async start(proposal: MatchProposal) {
    this.entered.push(proposal.id);
    await this.gate;
    return super.start(proposal);
  }

  open(): void {
    this.release();
  }
}

async function enqueue(
  h: Harness,
  queue: ResolvedQueue,
  options: {
    skill?: number;
    sim?: string;
    regions?: { region: string; rttMs: number }[];
    allowAi?: boolean;
    name?: string;
  } = {},
): Promise<{ accountId: string; ticketId: string }> {
  const accountId = await createAccount(database.db, options.name ?? `Player${++players}`);
  const result = await joinQueue(database.db, {
    accountId,
    queue,
    simVersion: options.sim ?? SIM_A,
    regions: options.regions ?? [{ region: 'eu-west', rttMs: 30 }],
    ...(options.allowAi === undefined ? {} : { allowAiOpponent: options.allowAi }),
    now: h.clock.now(),
  });
  if (!result.ok) throw new Error(`join failed: ${result.code}`);
  if (options.skill !== undefined) {
    await database.db
      .updateTable('queue_tickets')
      .set({ rating_mu: muFor(options.skill) })
      .where('id', '=', result.ticketId)
      .execute();
  }
  return { accountId, ticketId: result.ticketId };
}

async function ticketStatus(id: string) {
  return database.db
    .selectFrom('queue_tickets')
    .select(['status', 'created_at', 'match_id'])
    .where('id', '=', id)
    .executeTakeFirstOrThrow();
}

async function proposals() {
  return database.db.selectFrom('match_proposals').selectAll().orderBy('created_at').execute();
}

describe('grouping (pure)', () => {
  const t = (
    id: string,
    skill: number,
    regions = [{ region: 'eu', rttMs: 40 }],
    waited = 0,
  ): WaitingTicket => ({
    id,
    accountId: `acc-${id}`,
    createdAt: new Date(Date.parse('2026-10-01T12:00:00Z') - waited * 1000),
    mu: muFor(skill),
    sigma: 3,
    regions,
    allowAi: true,
  });
  const now = new Date('2026-10-01T12:00:00Z');

  it('always chooses a region, minimising the worst round trip', () => {
    const both = (a: { region: string; rttMs: number }[], b: { region: string; rttMs: number }[]) =>
      chooseRegion([{ regions: a }, { regions: b }]);
    expect(
      both(
        [
          { region: 'eu', rttMs: 20 },
          { region: 'us', rttMs: 120 },
        ],
        [
          { region: 'eu', rttMs: 140 },
          { region: 'us', rttMs: 60 },
        ],
      ),
    ).toEqual({ region: 'us', worst: 120 });
    // A single far relay is still a region.
    expect(both([{ region: 'ap', rttMs: 400 }], [{ region: 'ap', rttMs: 380 }])).toEqual({
      region: 'ap',
      worst: 400,
    });
    // No shared region: the best-covered, fastest one, with an unknown worst case.
    expect(both([{ region: 'eu', rttMs: 20 }], [{ region: 'us', rttMs: 30 }])).toEqual({
      region: 'eu',
      worst: Infinity,
    });
    // Missing measurements mean "any region".
    expect(chooseRegion([{ regions: [] }, { regions: [] }])).toEqual({ region: null, worst: 0 });
    expect(chooseRegion([{ regions: [] }, { regions: [{ region: 'us', rttMs: 90 }] }])).toEqual({
      region: 'us',
      worst: 90,
    });
  });

  it('relaxes the region preference with wait time', () => {
    expect(rttTolerance(RANKED, 0)).toBe(100);
    expect(rttTolerance(RANKED, 10)).toBe(150);
    expect(rttTolerance(RANKED, 30)).toBe(Infinity);
  });

  it('widens the rating window with wait time', () => {
    expect(ratingWindow(RANKED, 0)).toBe(100);
    expect(ratingWindow(RANKED, 40)).toBe(300);
    expect(ratingWindow(RANKED, 10_000)).toBe(800);
    expect(planGroups(RANKED, [t('a', 1500), t('b', 1800)], [], now)).toEqual([]);
    expect(planGroups(RANKED, [t('a', 1500, undefined, 40), t('b', 1800)], [], now)).toHaveLength(
      1,
    );
  });

  it('pairs each anchor with the closest compatible rating', () => {
    const groups = planGroups(
      RANKED,
      [t('a', 1500, undefined, 5), t('b', 1580), t('c', 1520), t('d', 1600)],
      [],
      now,
    );
    const ids = groups.map((g) =>
      g.sides.flat().map((m) => (m.kind === 'human' ? m.ticket.id : m.candidate.ai)),
    );
    expect(ids).toEqual([
      ['a', 'c'],
      ['b', 'd'],
    ]);
  });

  it('balances 2v2 sides by summed skill', () => {
    const members = [1900, 1500, 1600, 1800].map((s, i) => ({
      kind: 'human' as const,
      ticket: t(String(i), s),
    }));
    const sides = balanceSides(members).map((side) =>
      side.map((m) => (m.kind === 'human' ? m.ticket.id : '')).sort(),
    );
    expect(sides).toEqual([
      ['0', '1'],
      ['2', '3'],
    ]);
  });
});

describe('matchmaker', () => {
  it('only groups players of the same sim version', async () => {
    const h = harness();
    await enqueue(h, CASUAL, { sim: SIM_A });
    await enqueue(h, CASUAL, { sim: SIM_B });
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    const third = await enqueue(h, CASUAL, { sim: SIM_A });
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1, started: 1 });
    const [proposal] = await proposals();
    expect(proposal).toMatchObject({
      sim_version: SIM_A,
      status: 'started',
      rated: false,
      backfilled: false,
    });
    expect(h.starter.calls[0]!.simVersion).toBe(SIM_A);
    expect((await ticketStatus(third.ticketId)).status).toBe('matched');
  });

  it('plays on the region with the lowest worst-case round trip', async () => {
    const h = harness();
    await enqueue(h, CASUAL, {
      regions: [
        { region: 'eu-west', rttMs: 30 },
        { region: 'us-east', rttMs: 90 },
      ],
    });
    await enqueue(h, CASUAL, {
      regions: [
        { region: 'eu-west', rttMs: 180 },
        { region: 'us-east', rttMs: 70 },
      ],
    });
    await h.matchmaker.tick();
    expect(h.starter.calls[0]!.region).toBe('us-east');
  });

  it('still matches players whose only relay is far away', async () => {
    const h = harness();
    await enqueue(h, CASUAL, { regions: [{ region: 'ap-south', rttMs: 420 }] });
    await enqueue(h, CASUAL, { regions: [{ region: 'ap-south', rttMs: 380 }] });
    // A good shared relay is preferred at first ...
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    // ... but after anyRegionAfterSeconds the best relay that exists is used.
    h.clock.advance(30);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1, started: 1 });
    expect(h.starter.calls[0]!.region).toBe('ap-south');
  });

  it('matches tickets without RTT measurements at once, on any region', async () => {
    const h = harness();
    await enqueue(h, CASUAL, { regions: [] });
    await enqueue(h, CASUAL, { regions: [] });
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1 });
    expect(h.starter.calls[0]!.region).toBeNull();
    await enqueue(h, CASUAL, { regions: [] });
    await enqueue(h, CASUAL, { regions: [{ region: 'us-east', rttMs: 300 }] });
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    h.clock.advance(30);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1 });
    expect(h.starter.calls[1]!.region).toBe('us-east');
  });

  it('prefers a partner on a good shared relay early, and any partner later', async () => {
    const h = harness();
    const anchor = await enqueue(h, CASUAL, {
      skill: 1500,
      regions: [{ region: 'eu-west', rttMs: 30 }],
    });
    const farButEqual = await enqueue(h, CASUAL, {
      skill: 1500,
      regions: [{ region: 'us-east', rttMs: 40 }],
    });
    const nearby = await enqueue(h, CASUAL, {
      skill: 1550,
      regions: [{ region: 'eu-west', rttMs: 45 }],
    });
    await h.matchmaker.tick();
    const tickets = h.starter.calls[0]!.seats.map((s) => s.ticketId).sort();
    expect(tickets).toEqual([anchor.ticketId, nearby.ticketId].sort());
    expect(h.starter.calls[0]!.region).toBe('eu-west');

    // Alone with no shared relay, the remaining player is matched once the
    // preference has widened to any region.
    const late = await enqueue(h, CASUAL, { regions: [{ region: 'eu-west', rttMs: 20 }] });
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    h.clock.advance(30);
    await h.matchmaker.tick();
    const second = h.starter.calls[1]!;
    expect(second.seats.map((s) => s.ticketId).sort()).toEqual(
      [farButEqual.ticketId, late.ticketId].sort(),
    );
  });

  it('applies the opt-in hard RTT cap only when an operator sets it', async () => {
    const capped = resolveQueue({
      id: 'capped',
      name: 'Capped',
      mode: '1v1',
      rated: false,
      maxRttMs: 150,
    });
    const h = harness([capped]);
    await enqueue(h, capped, { regions: [{ region: 'ap-south', rttMs: 400 }] });
    await enqueue(h, capped, { regions: [{ region: 'ap-south', rttMs: 400 }] });
    h.clock.advance(600);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    expect(CASUAL.maxRttMs).toBeUndefined();
  });

  it('widens the rating window as tickets wait', async () => {
    const h = harness();
    await enqueue(h, RANKED, { skill: 1500 });
    await enqueue(h, RANKED, { skill: 1800 });
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    h.clock.advance(39);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    h.clock.advance(1);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1 });
  });

  it('backfills with the closest-rated AI after the delay, rated and without an accept step', async () => {
    const h = harness();
    const solo = await enqueue(h, RANKED, { skill: 1650 });
    h.clock.advance(89);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    const status = h.notifier.of('queue.status', solo.accountId).at(-1)!;
    expect(status.data).toMatchObject({
      waitedSeconds: 89,
      aiBackfillAt: '2026-10-01T12:01:30.000Z',
      region: 'eu-west',
      rttMs: 30,
      allowAiOpponent: true,
      backfillAi: { ai: 'nicowar' },
    });
    const range = status.data.ratingRange!;
    expect(range.min).toBeLessThan(1650);
    expect(range.max).toBeGreaterThan(1650);
    expect(Math.round((range.max - range.min) / 2)).toBe(status.data.ratingWindow);
    expect(checkDocument('RealtimeEventQueueStatus', status.data).stage).toBe('ok');
    h.clock.advance(1);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1, started: 1 });
    const call = h.starter.calls[0]!;
    expect(call).toMatchObject({ backfilled: true, rated: true, region: 'eu-west' });
    const ai = call.seats.find((s) => s.kind === 'ai')!;
    expect(ai.ai).toBe('nicowar'); // 1653 is closest to 1650
    expect(ai.mu).toBeCloseTo(aiSeedRating('nicowar').mu, 9);
    const shown = h.notifier.of('queue.proposal', solo.accountId)[0]!.data;
    expect(shown).toMatchObject({
      requiresAccept: false,
      ais: 1,
      rated: true,
      backfilled: true,
      map: { width: 128, height: 128 },
    });
    expect(shown.seats).toHaveLength(2);
    expect(shown.seats!.find((s) => s.kind === 'ai')).toMatchObject({
      displayName: 'Nicowar',
      ai: 'nicowar',
      response: 'not_required',
    });
    expect(shown.seats!.find((s) => s.you)?.kind).toBe('human');
    expect(checkDocument('RealtimeEventQueueProposal', shown).stage).toBe('ok');
    const match = await database.db
      .selectFrom('matches')
      .select(['rated', 'proposal_id'])
      .where('id', '=', (await ticketStatus(solo.ticketId)).match_id!)
      .executeTakeFirstOrThrow();
    expect(match).toEqual({ rated: true, proposal_id: call.id });
  });

  it('never backfills players who opted out', async () => {
    const h = harness();
    await enqueue(h, RANKED, { allowAi: false });
    h.clock.advance(3600 - 1);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
  });

  it('fills a 2v2 with humans first and AIs for the rest after the delay', async () => {
    const h = harness();
    await enqueue(h, RANKED_2V2, { skill: 1500 });
    await enqueue(h, RANKED_2V2, { skill: 1550 });
    h.clock.advance(119);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    h.clock.advance(1);
    await h.matchmaker.tick();
    const call = h.starter.calls[0]!;
    expect(call.seats.filter((s) => s.kind === 'human')).toHaveLength(2);
    const ais = call.seats.filter((s) => s.kind === 'ai').map((s) => s.ai);
    expect(new Set(ais).size).toBe(2); // distinct AIs
    expect(ais.sort()).toEqual(['cortex', 'warrush']); // 1601 and 1401 are closest to 1525
    expect(call.seats.filter((s) => s.side === 0)).toHaveLength(2);
  });

  it('starts a ranked all-human match only after both players accept', async () => {
    const h = harness();
    const a = await enqueue(h, RANKED);
    const b = await enqueue(h, RANKED);
    await h.matchmaker.tick();
    const [proposal] = await proposals();
    expect(proposal).toMatchObject({
      status: 'pending',
      expires_at: new Date('2026-10-01T12:00:10Z'),
    });
    expect(h.starter.calls).toHaveLength(0);
    expect(h.notifier.of('queue.proposal', a.accountId)[0]!.data).toMatchObject({
      requiresAccept: true,
      expiresAt: '2026-10-01T12:00:10.000Z',
    });
    expect(
      await respondToProposal(database.db, a.accountId, proposal!.id, true, h.clock.now()),
    ).toBe('recorded');
    expect(
      await respondToProposal(database.db, a.accountId, proposal!.id, true, h.clock.now()),
    ).toBe('already');
    h.clock.advance(3);
    expect(await h.matchmaker.tick()).toMatchObject({ started: 0 });
    await respondToProposal(database.db, b.accountId, proposal!.id, true, h.clock.now());
    // The API resends the prompt after each answer: both seats now show accepted.
    h.notifier.clear();
    await sendProposal(database.db, h.notifier, proposal!.id);
    const resent = h.notifier.of('queue.proposal', b.accountId)[0]!.data;
    expect(resent.seats!.map((s) => s.response)).toEqual(['accepted', 'accepted']);
    expect(resent.seats!.filter((s) => s.you)).toHaveLength(1);
    expect(h.notifier.of('queue.proposal', a.accountId)).toHaveLength(1);
    expect(await h.matchmaker.tick()).toMatchObject({ started: 1 });
    expect(h.notifier.of('queue.matchFound', b.accountId)).toHaveLength(1);
    expect((await ticketStatus(a.ticketId)).status).toBe('matched');
    // Later searchers see the typical wait of matched tickets.
    const c = await enqueue(h, RANKED);
    h.clock.advance(5);
    await h.matchmaker.tick();
    expect(h.notifier.of('queue.status', c.accountId).at(-1)!.data.typicalWaitSeconds).toBe(3);
  });

  it('changes "Allow an AI opponent" without losing the queue position', async () => {
    const h = harness();
    const solo = await enqueue(h, RANKED);
    expect(await updateTicket(database.db, solo.accountId, solo.ticketId, false)).toBe('updated');
    h.clock.advance(3600);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 0 });
    expect(await updateTicket(database.db, solo.accountId, solo.ticketId, true)).toBe('updated');
    h.clock.advance(1);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1 });
    expect(await updateTicket(database.db, solo.accountId, solo.ticketId, false)).toBe(
      'not_waiting',
    );
    expect(await updateTicket(database.db, solo.accountId, crypto.randomUUID(), false)).toBe(
      'not_found',
    );
  });

  it('removes a decliner with a cooldown and requeues the others at their original position', async () => {
    const h = harness();
    const a = await enqueue(h, RANKED, { name: 'Early' });
    h.clock.advance(5);
    const b = await enqueue(h, RANKED, { name: 'Decliner' });
    await h.matchmaker.tick();
    const [proposal] = await proposals();
    await respondToProposal(database.db, a.accountId, proposal!.id, true, h.clock.now());
    await respondToProposal(database.db, b.accountId, proposal!.id, false, h.clock.now());
    h.clock.advance(1);
    expect(await h.matchmaker.tick()).toMatchObject({ cancelled: 1, proposed: 0 });

    const declined = await ticketStatus(b.ticketId);
    expect(declined.status).toBe('declined');
    const requeued = await ticketStatus(a.ticketId);
    expect(requeued).toMatchObject({
      status: 'waiting',
      created_at: new Date('2026-10-01T12:00:00Z'),
    });
    expect(h.notifier.of('queue.proposalEnded', b.accountId)[0]!.data).toMatchObject({
      outcome: 'removed',
      reason: 'declined',
      cooldownUntil: '2026-10-01T12:01:06.000Z',
    });
    expect(h.notifier.of('queue.proposalEnded', a.accountId)[0]!.data).toMatchObject({
      outcome: 'requeued',
      reason: 'other_declined',
    });
    const blocked = await joinQueue(database.db, {
      accountId: b.accountId,
      queue: RANKED,
      simVersion: SIM_A,
      regions: [],
      now: h.clock.now(),
    });
    expect(blocked).toMatchObject({ ok: false, code: 'cooldown' });

    // The requeued player keeps their place ahead of later arrivals.
    h.clock.advance(1);
    const c = await enqueue(h, RANKED, { name: 'Late1' });
    const d = await enqueue(h, RANKED, { name: 'Late2', skill: 1500 });
    await h.matchmaker.tick();
    const seats = await database.db
      .selectFrom('match_proposal_seats')
      .innerJoin('match_proposals', 'match_proposals.id', 'match_proposal_seats.proposal_id')
      .select(['match_proposal_seats.ticket_id'])
      .where('match_proposals.status', '=', 'pending')
      .execute();
    expect(seats.map((s) => s.ticket_id)).toContain(a.ticketId);
    expect(
      [c.ticketId, d.ticketId].filter((id) => seats.some((s) => s.ticket_id === id)),
    ).toHaveLength(1);
  });

  it('times out players who do not answer', async () => {
    const h = harness();
    const a = await enqueue(h, RANKED);
    const b = await enqueue(h, RANKED);
    await h.matchmaker.tick();
    const [proposal] = await proposals();
    await respondToProposal(database.db, a.accountId, proposal!.id, true, h.clock.now());
    h.clock.advance(9);
    expect(await h.matchmaker.tick()).toMatchObject({ cancelled: 0 });
    h.clock.advance(1);
    expect(
      await respondToProposal(database.db, b.accountId, proposal!.id, true, h.clock.now()),
    ).toBe('not_pending');
    expect(await h.matchmaker.tick()).toMatchObject({ cancelled: 1 });
    expect((await ticketStatus(b.ticketId)).status).toBe('declined');
    expect((await ticketStatus(a.ticketId)).status).toBe('waiting');
    expect(h.notifier.of('queue.proposalEnded', b.accountId)[0]!.data.reason).toBe('timeout');
    const seats = await database.db
      .selectFrom('match_proposal_seats')
      .select(['account_id', 'response'])
      .where('proposal_id', '=', proposal!.id)
      .execute();
    expect(seats.find((s) => s.account_id === b.accountId)!.response).toBe('timeout');
  });

  it('treats leaving during an accept prompt as declining', async () => {
    const h = harness();
    const a = await enqueue(h, RANKED);
    const b = await enqueue(h, RANKED);
    await h.matchmaker.tick();
    expect(await leaveQueue(database.db, b.accountId, b.ticketId, h.clock.now())).toBe('declined');
    await h.matchmaker.tick();
    expect((await ticketStatus(b.ticketId)).status).toBe('declined');
    expect((await ticketStatus(a.ticketId)).status).toBe('waiting');
    expect(await leaveQueue(database.db, a.accountId, a.ticketId, h.clock.now())).toBe('left');
  });

  it('requeues players when the match cannot start after retries', async () => {
    const h = harness();
    h.starter.failNext = 3;
    const a = await enqueue(h, CASUAL);
    await enqueue(h, CASUAL);
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1, started: 0, failed: 0 });
    expect(await h.matchmaker.tick()).toMatchObject({ failed: 0 });
    expect(await h.matchmaker.tick()).toMatchObject({ failed: 1 });
    expect((await proposals())[0]).toMatchObject({ status: 'failed', start_attempts: 3 });
    expect((await ticketStatus(a.ticketId)).status).toBe('waiting');
    expect(h.notifier.of('queue.proposalEnded', a.accountId)[0]!.data.reason).toBe('start_failed');
    // Next tick they are grouped again and start.
    expect(await h.matchmaker.tick()).toMatchObject({ proposed: 1, started: 1 });
  });

  it('keeps guests out of rated queues and allows one active ticket per account', async () => {
    const h = harness();
    const guest = await createAccount(database.db, 'Guest', 'guest');
    const join = (queue: ResolvedQueue, accountId: string) =>
      joinQueue(database.db, {
        accountId,
        queue,
        simVersion: SIM_A,
        regions: [],
        now: h.clock.now(),
      });
    expect(await join(RANKED, guest)).toEqual({ ok: false, code: 'guest_not_allowed' });
    expect(await join(CASUAL, guest)).toMatchObject({ ok: true });
    expect(await join(CASUAL, guest)).toEqual({ ok: false, code: 'already_queued' });
  });
});

describe('matchmaker liveness', () => {
  it('keeps grouping while a start waits for its map, and starts each proposal once', async () => {
    const clock = new FakeClock();
    const starter = new GatedStarter(database.db);
    const matchmaker = new Matchmaker({
      db: database.db,
      queues: [CASUAL],
      starter,
      notifier: new RecordingQueueNotifier(),
      clock,
      random: () => 0,
      startWaitMs: 20,
    });
    const h = { clock } as Harness;
    await enqueue(h, CASUAL);
    await enqueue(h, CASUAL);
    const began = Date.now();
    expect(await matchmaker.tick()).toMatchObject({ proposed: 1, started: 0 });
    expect(Date.now() - began).toBeLessThan(2000);
    expect(matchmaker.startsInFlight).toBe(1);

    // More players arrive while the first start is still waiting.
    await enqueue(h, CASUAL);
    await enqueue(h, CASUAL);
    expect(await matchmaker.tick()).toMatchObject({ proposed: 1, started: 0 });
    expect(await matchmaker.tick()).toMatchObject({ proposed: 0, started: 0 });
    expect(matchmaker.startsInFlight).toBe(2);
    expect(new Set(starter.entered).size).toBe(2);
    expect(starter.entered).toHaveLength(2);

    starter.open();
    await matchmaker.waitForStarts();
    expect(await matchmaker.tick()).toMatchObject({ started: 2 });
    expect((await proposals()).map((p) => p.status)).toEqual(['started', 'started']);
  });

  it('commits nothing once the leader lease is lost', async () => {
    const h = harness([CASUAL]);
    const fenced = new Matchmaker({
      db: database.db,
      queues: [CASUAL],
      starter: h.starter,
      notifier: h.notifier,
      clock: h.clock,
      random: () => 0,
      fence: async () => {
        throw new LeaderLostError('scheduler epoch 3 is no longer current');
      },
    });
    await enqueue(h, CASUAL);
    await enqueue(h, CASUAL);
    await expect(fenced.tick()).rejects.toBeInstanceOf(LeaderLostError);
    expect(await proposals()).toEqual([]);
    expect(h.notifier.events).toEqual([]);
  });
});

describe('matchmaker leader failover', () => {
  it('runs on one leader at a time and a new leader resumes open proposals', async () => {
    const clock = new FakeClock();
    const logger = createLogger('test', 'silent');
    const leaders = ['first', 'second'].map((name) => {
      const h = harness([RANKED], clock);
      const leader = new LeaderElection({
        connectionString: database.url,
        name: 'scheduler',
        retryMs: 50,
        lead: (signal) =>
          runScheduler(
            [{ name: `matchmaker-${name}`, intervalMs: 20, run: () => h.matchmaker.tick() }],
            signal,
            logger,
          ),
      });
      return { h, leader };
    });
    const [first, second] = leaders as [(typeof leaders)[0], (typeof leaders)[0]];
    try {
      first.leader.start();
      await waitFor(() => first.leader.isLeader);
      second.leader.start();
      await new Promise((resolve) => setTimeout(resolve, 200));
      expect(second.leader.isLeader).toBe(false);

      const a = await enqueue(first.h, RANKED);
      const b = await enqueue(first.h, RANKED);
      await waitFor(async () => (await proposals()).some((p) => p.status === 'pending'));

      // The leader's database session dies (crash, network partition).
      await sql`SELECT pg_terminate_backend(pid) FROM pg_locks WHERE locktype = 'advisory' AND granted AND database = (SELECT oid FROM pg_database WHERE datname = current_database())`.execute(
        database.db,
      );
      await waitFor(() => second.leader.isLeader);
      // The old leader notices its lost session asynchronously; until then
      // both may tick, which the matchmaker's conditional updates tolerate.
      await waitFor(() => !first.leader.isLeader);

      const [proposal] = await proposals();
      await respondToProposal(database.db, a.accountId, proposal!.id, true, clock.now());
      await respondToProposal(database.db, b.accountId, proposal!.id, true, clock.now());
      await waitFor(async () => (await proposals())[0]!.status === 'started');
      expect(second.h.starter.calls.map((p) => p.id)).toEqual([proposal!.id]);
      expect(first.h.starter.calls).toHaveLength(0);
      expect((await ticketStatus(a.ticketId)).status).toBe('matched');
    } finally {
      await Promise.all(leaders.map(({ leader }) => leader.stop()));
    }
  });
});
