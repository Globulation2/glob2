import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { rate } from 'openskill';
import type { VerifiedOutcome } from '@glob2/protocol';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import {
  AI_SEED_ELO,
  AI_SEED_SIGMA,
  BETA,
  DISPLAY_PER_MU,
  MU0,
  OPENSKILL_OPTIONS,
  PROVISIONAL_SIGMA,
  SIGMA0,
  TAU,
  aiSeedRating,
  displayRating,
  isProvisional,
  matchSkill,
  rateSides,
} from '../src/ratings/scale.ts';
import {
  MUTUAL_LEAVE_TICKS,
  contestedTeams,
  decideRating,
  isEmptySeat,
  participantOutcomes,
  type OutcomeInput,
} from '../src/ratings/outcome.ts';
import { aiLadderRatings, ensureAiEntity, type RatedAi } from '../src/ratings/entities.ts';
import {
  applyMatchRatings,
  applyPendingRatings,
  handleEngineJobResult,
  recordVerification,
} from '../src/ratings/apply.ts';
import { matchRatingPreview } from '../src/ratings/preview.ts';
import {
  SIM_A,
  SIM_B,
  createAccount,
  createMatch,
  createVerifyJob,
  ratingOf,
  resultPayload,
  verified,
} from './support.ts';

type R = { mu: number; sigma: number };

/**
 * Independent Plackett-Luce update for two teams with distinct ranks (Weng &
 * Lin 2011, Algorithm 4, with the default γ = σ_i/c and τ added first),
 * written from the paper rather than the library, to check rateSides.
 */
function referencePL(teams: R[][], ranks: number[], tau = TAU): R[][] {
  const withTau = teams.map((t) =>
    t.map((r) => ({ mu: r.mu, sigma: Math.sqrt(r.sigma ** 2 + tau ** 2) })),
  );
  const mu = withTau.map((t) => t.reduce((s, r) => s + r.mu, 0));
  const s2 = withTau.map((t) => t.reduce((s, r) => s + r.sigma ** 2, 0));
  const c = Math.sqrt(s2.reduce((a, v) => a + v + BETA ** 2, 0));
  const e = mu.map((m) => Math.exp(m / c));
  // C_q: sum over teams ranked no better than q.
  const C = ranks.map((rq) => ranks.reduce((a, ri, i) => (ri >= rq ? a + e[i]! : a), 0));
  return withTau.map((team, i) => {
    let omega = 0;
    let delta = 0;
    ranks.forEach((rq, q) => {
      if (rq > ranks[i]!) return;
      const p = e[i]! / C[q]!;
      omega += (i === q ? 1 : 0) - p;
      delta += p * (1 - p);
    });
    const gamma = Math.sqrt(s2[i]!) / c;
    const Omega = (s2[i]! / c) * omega;
    const Delta = gamma * (s2[i]! / c ** 2) * delta;
    return team.map((r) => ({
      mu: r.mu + (r.sigma ** 2 / s2[i]!) * Omega,
      sigma: r.sigma * Math.sqrt(Math.max(1 - (r.sigma ** 2 / s2[i]!) * Delta, 1e-4)),
    }));
  });
}

function expectClose(actual: R[][], expected: R[][], digits = 10) {
  actual.forEach((team, i) =>
    team.forEach((r, j) => {
      expect(r.mu).toBeCloseTo(expected[i]![j]!.mu, digits);
      expect(r.sigma).toBeCloseTo(expected[i]![j]!.sigma, digits);
    }),
  );
}

describe('rating maths', () => {
  it('pins the model options', () => {
    expect(OPENSKILL_OPTIONS).toMatchObject({ mu: 25, sigma: 25 / 3, beta: 25 / 6, tau: 25 / 300 });
  });

  it('reproduces the published OpenSkill 2v2 vector (τ = 0)', () => {
    // openskill README: a1 default + a2 beat b1 + b2.
    const [[x1, x2], [y1, y2]] = rate(
      [
        [
          { mu: 25, sigma: 25 / 3 },
          { mu: 32.444, sigma: 5.123 },
        ],
        [
          { mu: 43.381, sigma: 2.421 },
          { mu: 25.188, sigma: 6.211 },
        ],
      ],
      { ...OPENSKILL_OPTIONS, tau: 0 },
    );
    expect(x1!.mu).toBeCloseTo(28.669648436582808, 9);
    expect(x1!.sigma).toBeCloseTo(8.071520788025197, 9);
    expect(x2!.mu).toBeCloseTo(33.83086971107981, 9);
    expect(x2!.sigma).toBeCloseTo(5.062772998705765, 9);
    expect(y1!.mu).toBeCloseTo(43.071274808241974, 9);
    expect(y1!.sigma).toBeCloseTo(2.4166900452721256, 9);
    expect(y2!.mu).toBeCloseTo(23.149503312339064, 9);
    expect(y2!.sigma).toBeCloseTo(6.1378606973362135, 9);
  });

  it('matches an independent Plackett-Luce implementation (1v1 and 2v2, with τ)', () => {
    const cases: { teams: R[][]; ranks: number[] }[] = [
      { teams: [[{ mu: 25, sigma: SIGMA0 }], [{ mu: 25, sigma: SIGMA0 }]], ranks: [1, 2] },
      { teams: [[{ mu: 30, sigma: 3 }], [{ mu: 22, sigma: 6 }]], ranks: [2, 1] },
      {
        teams: [
          [
            { mu: 27, sigma: 4 },
            { mu: 20, sigma: 7 },
          ],
          [
            { mu: 31, sigma: 2.5 },
            { mu: 25, sigma: SIGMA0 },
          ],
        ],
        ranks: [1, 2],
      },
    ];
    for (const { teams, ranks } of cases) {
      expectClose(rateSides(teams, ranks), referencePL(teams, ranks));
    }
    // Known value: two new players, the winner gains ~2.635 μ.
    const newPair = rateSides([[{ mu: MU0, sigma: SIGMA0 }], [{ mu: MU0, sigma: SIGMA0 }]], [1, 2]);
    const winner = newPair[0]![0];
    const loser = newPair[1]![0];
    expect(winner!.mu).toBeCloseTo(27.6352, 3);
    expect(loser!.mu).toBeCloseTo(22.3648, 3);
    expect(winner!.sigma).toBeCloseTo(loser!.sigma, 12);
  });

  it('scales μ like Elo: 400 display points are tenfold odds between settled players', () => {
    const c = Math.SQRT2 * BETA;
    const deltaMu = 400 / DISPLAY_PER_MU;
    const p = 1 / (1 + Math.exp(-deltaMu / c));
    expect(p).toBeCloseTo(10 / 11, 12);
    expect(DISPLAY_PER_MU).toBeCloseTo(29.4805, 3);
  });

  it('displays a 1500-centred ordinal with a provisional flag', () => {
    expect(displayRating({ mu: MU0, sigma: PROVISIONAL_SIGMA })).toBeCloseTo(1500, 9);
    expect(displayRating({ mu: MU0, sigma: SIGMA0 })).toBeCloseTo(1500 - 10 * DISPLAY_PER_MU, 9);
    expect(isProvisional({ mu: MU0, sigma: SIGMA0 })).toBe(true);
    expect(isProvisional({ mu: MU0, sigma: PROVISIONAL_SIGMA })).toBe(false);
    expect(matchSkill({ mu: MU0 })).toBe(1500);
  });

  it('seeds AIs at their documented Elo with high uncertainty', () => {
    for (const [ai, elo] of Object.entries(AI_SEED_ELO)) {
      const seed = aiSeedRating(ai as RatedAi);
      expect(matchSkill(seed)).toBeCloseTo(elo, 9);
      expect(seed.sigma).toBe(AI_SEED_SIGMA);
      expect(isProvisional(seed)).toBe(true);
    }
    expect(aiSeedRating('maxima').mu).toBeCloseTo(25 + 373 / DISPLAY_PER_MU, 9);
  });
});

describe('rating decision', () => {
  const duel = (overrides: Partial<OutcomeInput> = {}): OutcomeInput => ({
    teamAlliance: new Map([
      [0, 0],
      [1, 1],
    ]),
    participants: [
      { seat: 0, team: 0, kind: 'human' },
      { seat: 1, team: 1, kind: 'human' },
    ],
    teamOutcomes: new Map([
      [0, 'won'],
      [1, 'lost'],
    ]),
    finalTick: 10_000,
    ...overrides,
  });
  const unresolved = new Map([
    [0, 'unresolved' as const],
    [1, 'unresolved' as const],
  ]);

  it('rates a verified winner', () => {
    expect(decideRating(duel())).toEqual({
      kind: 'rate',
      sides: [0, 1],
      ranks: [1, 2],
      reason: 'verified',
    });
  });

  it('treats the first side to abandon as the loser', () => {
    const decision = decideRating(
      duel({
        teamOutcomes: unresolved,
        participants: [
          { seat: 0, team: 0, kind: 'human', quitTick: 4000 },
          { seat: 1, team: 1, kind: 'human', quitTick: 4000 + MUTUAL_LEAVE_TICKS + 1 },
        ],
      }),
    );
    expect(decision).toMatchObject({
      kind: 'rate',
      ranks: [2, 1],
      reason: 'abandoned',
      abandonedSide: 0,
    });
  });

  it('leaves ratings alone on mutual leave and on unresolved games', () => {
    expect(
      decideRating(
        duel({
          teamOutcomes: unresolved,
          participants: [
            { seat: 0, team: 0, kind: 'human', quitTick: 4000 },
            { seat: 1, team: 1, kind: 'human', quitTick: 4000 + MUTUAL_LEAVE_TICKS },
          ],
        }),
      ),
    ).toEqual({ kind: 'unchanged', reason: 'mutual_leave' });
    expect(decideRating(duel({ teamOutcomes: unresolved }))).toEqual({
      kind: 'unchanged',
      reason: 'unresolved',
    });
  });

  it('never counts an AI side as leaving', () => {
    const decision = decideRating(
      duel({
        teamOutcomes: unresolved,
        participants: [
          { seat: 0, team: 0, kind: 'human', abandoned: true },
          { seat: 1, team: 1, kind: 'ai' },
        ],
      }),
    );
    expect(decision).toMatchObject({ kind: 'rate', ranks: [2, 1], abandonedSide: 0 });
  });

  it('groups 2v2 teams by alliance; a side leaves only when its last human does', () => {
    const teamAlliance = new Map([
      [0, 0],
      [1, 0],
      [2, 1],
      [3, 1],
    ]);
    const base = {
      teamAlliance,
      finalTick: 20_000,
      teamOutcomes: new Map([
        [0, 'lost' as const],
        [1, 'won' as const],
        [2, 'lost' as const],
        [3, 'lost' as const],
      ]),
      participants: [0, 1, 2, 3].map((seat) => ({ seat, team: seat, kind: 'human' as const })),
    };
    expect(decideRating(base)).toMatchObject({ kind: 'rate', sides: [0, 1], ranks: [1, 2] });
    const partialLeave = decideRating({
      ...base,
      teamOutcomes: new Map([0, 1, 2, 3].map((t) => [t, 'unresolved' as const])),
      participants: base.participants.map((p) => (p.seat === 2 ? { ...p, quitTick: 100 } : p)),
    });
    expect(partialLeave).toEqual({ kind: 'unchanged', reason: 'unresolved' });
  });
});

describe('shared wins', () => {
  // The live staging match: four free-for-all teams, sudden death, nobody
  // with prestige at the buzzer. The engine reports all four as won.
  const ffa = new Map([
    [0, 0],
    [1, 1],
    [2, 2],
    [3, 3],
  ]);

  it('records a win shared by several alliances as a draw for those teams', () => {
    const allWon = new Map([0, 1, 2, 3].map((t) => [t, 'won' as const]));
    expect([...participantOutcomes(ffa, allWon)]).toEqual([
      [0, 'draw'],
      [1, 'draw'],
      [2, 'draw'],
      [3, 'draw'],
    ]);
    // A tie at the top between two of four: the others lost.
    const tieAtTop = new Map([
      [0, 'won' as const],
      [1, 'lost' as const],
      [2, 'won' as const],
      [3, 'lost' as const],
    ]);
    expect([...participantOutcomes(ffa, tieAtTop).values()]).toEqual([
      'draw',
      'lost',
      'draw',
      'lost',
    ]);
  });

  it('keeps a win held by one alliance, however many of its teams won', () => {
    const twoVsTwo = new Map([
      [0, 0],
      [1, 0],
      [2, 1],
      [3, 1],
    ]);
    const alliesWon = new Map([
      [0, 'won' as const],
      [1, 'won' as const],
      [2, 'lost' as const],
      [3, 'unresolved' as const],
    ]);
    expect([...participantOutcomes(twoVsTwo, alliesWon).values()]).toEqual([
      'won',
      'won',
      'lost',
      'unresolved',
    ]);
    const single = new Map([
      [0, 'won' as const],
      [1, 'lost' as const],
    ]);
    expect([...participantOutcomes(ffa, single).values()]).toEqual(['won', 'lost']);
  });

  it('does not rate a draw, whether the outcomes are raw or recorded', () => {
    const teamAlliance = new Map([
      [0, 0],
      [1, 1],
    ]);
    const participants = [
      { seat: 0, team: 0, kind: 'human' as const },
      { seat: 1, team: 1, kind: 'ai' as const },
    ];
    const raw = new Map([
      [0, 'won' as const],
      [1, 'won' as const],
    ]);
    const input = { teamAlliance, participants, teamOutcomes: raw, finalTick: 6000 };
    expect(decideRating(input)).toEqual({ kind: 'unchanged', reason: 'draw' });
    expect(
      decideRating({ ...input, teamOutcomes: participantOutcomes(teamAlliance, raw) }),
    ).toEqual({ kind: 'unchanged', reason: 'draw' });
    // A draw is a verified result: an earlier leaver does not turn it into a loss.
    expect(
      decideRating({
        ...input,
        participants: [{ ...participants[0]!, quitTick: 100 }, participants[1]!],
      }),
    ).toEqual({ kind: 'unchanged', reason: 'draw' });
  });
});

describe('closed seats', () => {
  // A room on a four-team map with room seats 1 and 3 empty: the players are
  // seats 0 and 1 on teams 0 and 2, and teams 1 and 3 are closed. The engine
  // removes closed colonies at the start, so they have lost and the last
  // player standing wins outright (opponents defeated).
  const ffa = new Map([
    [0, 0],
    [1, 1],
    [2, 2],
    [3, 3],
  ]);
  const seats = [
    { team: 0, kind: 'human' as const },
    { team: 2, kind: 'human' as const },
    { team: 1, kind: 'closed' as const },
    { team: 3, kind: 'closed' as const },
  ];
  const outcomes = (o: ('won' | 'lost' | 'unresolved')[]) =>
    new Map(o.map((outcome, team) => [team, outcome]));

  it('are empty seats: never contested, never participants', () => {
    expect([...contestedTeams(seats)].sort()).toEqual([0, 2]);
    expect(seats.filter(isEmptySeat).map((s) => s.team)).toEqual([1, 3]);
  });

  it('a player who eliminates the other wins; the closed teams lost', () => {
    const recorded = participantOutcomes(
      ffa,
      outcomes(['won', 'lost', 'lost', 'lost']),
      contestedTeams(seats),
    );
    expect([...recorded.values()]).toEqual(['won', 'lost', 'lost', 'lost']);
    const participants = [
      { seat: 0, team: 0, kind: 'human' as const },
      { seat: 1, team: 2, kind: 'human' as const },
    ];
    expect(
      decideRating({
        teamAlliance: ffa,
        participants,
        teamOutcomes: recorded,
        finalTick: 2400,
      }),
    ).toEqual({ kind: 'rate', sides: [0, 2], ranks: [1, 2], reason: 'verified' });
  });
});

describe('empty seats', () => {
  // A room on a four-team map: seats 2 and 3 empty, sent as AI `none`
  // ("Nobody"). Their colonies sit alive and idle, so a sudden-death buzzer
  // at zero prestige reports them as won.
  const ffa = new Map([
    [0, 0],
    [1, 1],
    [2, 2],
    [3, 3],
  ]);
  const seats = [
    { team: 0, kind: 'human' as const },
    { team: 1, kind: 'human' as const },
    { team: 2, kind: 'ai' as const, ai: 'none' },
    { team: 3, kind: 'ai' as const, ai: 'none' },
  ];
  const contested = contestedTeams(seats);
  const outcomes = (o: ('won' | 'lost' | 'unresolved')[]) =>
    new Map(o.map((outcome, team) => [team, outcome]));

  it('counts only teams someone plays', () => {
    expect([...contested].sort()).toEqual([0, 1]);
    expect([...contestedTeams([...seats, { team: 3, kind: 'ai', ai: 'nicowar' }])].sort()).toEqual([
      0, 1, 3,
    ]);
  });

  it('1v1 win: the last player standing wins, the idle colonies do not', () => {
    // The staging room match: the guest left (team dead, lost), sudden death.
    const recorded = participantOutcomes(ffa, outcomes(['won', 'lost', 'won', 'won']), contested);
    expect([...recorded.values()]).toEqual(['won', 'lost', 'unresolved', 'unresolved']);
  });

  it('1v1 loss: the other player won', () => {
    const recorded = participantOutcomes(ffa, outcomes(['lost', 'won', 'won', 'lost']), contested);
    expect([...recorded.values()]).toEqual(['lost', 'won', 'unresolved', 'lost']);
  });

  it('two players tied at the top across alliances still draw', () => {
    const recorded = participantOutcomes(ffa, outcomes(['won', 'won', 'won', 'won']), contested);
    expect([...recorded.values()]).toEqual(['draw', 'draw', 'unresolved', 'unresolved']);
  });

  it('allies who won together win, even next to idle colonies', () => {
    const twoVsTwo = new Map([
      [0, 0],
      [1, 0],
      [2, 1],
      [3, 1],
      [4, 2],
      [5, 3],
    ]);
    const sixSeats = [
      ...[0, 1, 2, 3].map((team) => ({ team, kind: 'human' as const })),
      { team: 4, kind: 'ai' as const, ai: 'none' },
      { team: 5, kind: 'ai' as const, ai: 'none' },
    ];
    const recorded = participantOutcomes(
      twoVsTwo,
      new Map([
        [0, 'won' as const],
        [1, 'won' as const],
        [2, 'lost' as const],
        [3, 'lost' as const],
        [4, 'won' as const],
        [5, 'won' as const],
      ]),
      contestedTeams(sixSeats),
    );
    expect([...recorded.values()]).toEqual([
      'won',
      'won',
      'lost',
      'lost',
      'unresolved',
      'unresolved',
    ]);
  });

  it('without a seat list every team counts (older callers)', () => {
    const recorded = participantOutcomes(ffa, outcomes(['won', 'lost', 'won', 'won']));
    expect([...recorded.values()]).toEqual(['draw', 'lost', 'draw', 'draw']);
  });

  it('rating decisions ignore empty seats', () => {
    const participants = [
      { seat: 0, team: 0, kind: 'human' as const },
      { seat: 1, team: 1, kind: 'human' as const },
      { seat: 2, team: 2, kind: 'ai' as const, ai: 'none' },
      { seat: 3, team: 3, kind: 'ai' as const, ai: 'none' },
    ];
    const input = {
      teamAlliance: ffa,
      participants,
      teamOutcomes: outcomes(['won', 'lost', 'won', 'won']),
      finalTick: 1505,
    };
    expect(decideRating(input)).toEqual({
      kind: 'rate',
      sides: [0, 1],
      ranks: [1, 2],
      reason: 'verified',
    });
    // Abandonment: no verified winner, the guest left; an idle colony is
    // not an AI that "never leaves".
    expect(
      decideRating({
        ...input,
        teamOutcomes: outcomes(['unresolved', 'unresolved', 'unresolved', 'unresolved']),
        participants: participants.map((p) => (p.seat === 1 ? { ...p, quitTick: 1002 } : p)),
      }),
    ).toMatchObject({ kind: 'rate', ranks: [1, 2], reason: 'abandoned', abandonedSide: 1 });
  });
});

describe('rating application', () => {
  let database: TestDatabase;
  beforeAll(async () => {
    database = await createTestDatabase({ role: 'worker' });
  });
  afterAll(async () => {
    await database?.drop();
  });

  it('applies a verified 1v1 once, even when the verdict is delivered twice', async () => {
    const db = database.db;
    const alice = await createAccount(db, 'Alice');
    const bob = await createAccount(db, 'Bob');
    const matchId = await createMatch(db, [
      { side: 0, accountId: alice },
      { side: 1, accountId: bob },
    ]);
    const jobId = await createVerifyJob(db, matchId);
    const payload = resultPayload(jobId, verified(['won', 'lost']));

    expect(await handleEngineJobResult(db, payload)).toBe(true);
    expect(await handleEngineJobResult(db, payload)).toBe(false);
    expect(await applyMatchRatings(db, matchId)).toEqual({ status: 'already' });
    expect(await recordVerification(db, jobId)).toEqual({
      recorded: false,
      reason: 'already_recorded',
    });

    const a = await ratingOf(db, alice);
    const b = await ratingOf(db, bob);
    const expected = rateSides(
      [[{ mu: MU0, sigma: SIGMA0 }], [{ mu: MU0, sigma: SIGMA0 }]],
      [1, 2],
    );
    const expectedA = expected[0]![0];
    const expectedB = expected[1]![0];
    expect(a).toMatchObject({ games: 1, wins: 1 });
    expect(b).toMatchObject({ games: 1, wins: 0 });
    expect(a!.mu).toBeCloseTo(expectedA!.mu, 9);
    expect(b!.mu).toBeCloseTo(expectedB!.mu, 9);

    const match = await db
      .selectFrom('matches')
      .selectAll()
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(match).toMatchObject({
      verification: 'verified',
      rating_status: 'applied',
      rating_note: 'verified',
    });
    const participants = await db
      .selectFrom('match_participants')
      .selectAll()
      .where('match_id', '=', matchId)
      .orderBy('seat')
      .execute();
    expect(participants.map((p) => p.outcome)).toEqual(['won', 'lost']);
    expect(participants[0]!.rating_before).toBeCloseTo(
      displayRating({ mu: MU0, sigma: SIGMA0 }),
      9,
    );
    expect(participants[0]!.rating_after).toBeCloseTo(displayRating(expectedA!), 9);
    expect(participants[1]!.rating_after).toBeCloseTo(displayRating(expectedB!), 9);
    expect(participants.every((p) => p.rating_entity_id !== null)).toBe(true);
    const history = await db
      .selectFrom('rating_history')
      .selectAll()
      .where('match_id', '=', matchId)
      .execute();
    expect(history).toHaveLength(2);
    const stats = await db
      .selectFrom('match_team_stats')
      .selectAll()
      .where('match_id', '=', matchId)
      .execute();
    expect(stats.map((s) => s.outcome).sort()).toEqual(['lost', 'won']);
  });

  it('applies concurrent deliveries exactly once', async () => {
    const db = database.db;
    const p = await createAccount(db, 'P');
    const q = await createAccount(db, 'Q');
    const matchId = await createMatch(db, [
      { side: 0, accountId: p },
      { side: 1, accountId: q },
    ]);
    await db
      .updateTable('matches')
      .set({ verification: 'verified' })
      .where('id', '=', matchId)
      .execute();
    await db
      .insertInto('match_team_stats')
      .values([
        { match_id: matchId, team: 0, outcome: 'lost' },
        { match_id: matchId, team: 1, outcome: 'won' },
      ])
      .execute();
    const results = await Promise.all([1, 2, 3, 4].map(() => applyMatchRatings(db, matchId)));
    expect(results.filter((r) => r.status === 'applied')).toHaveLength(1);
    expect(results.filter((r) => r.status === 'already')).toHaveLength(3);
    expect(await ratingOf(db, q)).toMatchObject({ games: 1, wins: 1 });
  });

  it('records a shared win as a draw and leaves ratings unchanged', async () => {
    const db = database.db;
    const d1 = await createAccount(db, 'Draw1');
    const d2 = await createAccount(db, 'Draw2');
    const matchId = await createMatch(db, [
      { side: 0, accountId: d1 },
      { side: 1, accountId: d2 },
    ]);
    const job = await createVerifyJob(db, matchId);
    await handleEngineJobResult(db, resultPayload(job, verified(['won', 'won'])));
    const participants = await db
      .selectFrom('match_participants')
      .select(['outcome', 'rating_after'])
      .where('match_id', '=', matchId)
      .orderBy('seat')
      .execute();
    expect(participants).toEqual([
      { outcome: 'draw', rating_after: null },
      { outcome: 'draw', rating_after: null },
    ]);
    const stats = await db
      .selectFrom('match_team_stats')
      .select('outcome')
      .where('match_id', '=', matchId)
      .orderBy('team')
      .execute();
    expect(stats.map((s) => s.outcome)).toEqual(['draw', 'draw']);
    const row = await db
      .selectFrom('matches')
      .select(['rating_status', 'rating_note'])
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(row).toEqual({ rating_status: 'unchanged', rating_note: 'draw' });
    expect(await ratingOf(db, d1)).toBeUndefined();
  });

  it('records a room 1v1 on a four-team map as a win, not a draw, despite empty seats', async () => {
    const db = database.db;
    const host = await createAccount(db, 'Host');
    const guest = await createAccount(db, 'Guest');
    const nobody = 'none' as never;
    const room = (quitTick?: number) =>
      createMatch(
        db,
        [
          { side: 0, accountId: host },
          { side: 1, accountId: guest, ...(quitTick ? { quitTick, abandoned: true } : {}) },
          { side: 2, ai: nobody },
          { side: 3, ai: nobody },
        ],
        { queueId: null, rated: false },
      );
    const outcomesOf = async (matchId: string) => {
      const participants = await db
        .selectFrom('match_participants')
        .select('outcome')
        .where('match_id', '=', matchId)
        .orderBy('seat')
        .execute();
      const stats = await db
        .selectFrom('match_team_stats')
        .select('outcome')
        .where('match_id', '=', matchId)
        .orderBy('team')
        .execute();
      return {
        participants: participants.map((p) => p.outcome),
        teams: stats.map((s) => s.outcome),
      };
    };

    // Win: the guest's colony died, sudden death left the host and both idle colonies on top.
    const win = await room();
    await handleEngineJobResult(
      db,
      resultPayload(await createVerifyJob(db, win), verified(['won', 'lost', 'won', 'won'])),
    );
    expect(await outcomesOf(win)).toEqual({
      participants: ['won', 'lost', 'unresolved', 'unresolved'],
      teams: ['won', 'lost', 'unresolved', 'unresolved'],
    });

    // Loss, from the host's side.
    const loss = await room();
    await handleEngineJobResult(
      db,
      resultPayload(await createVerifyJob(db, loss), verified(['lost', 'won', 'won', 'won'])),
    );
    expect((await outcomesOf(loss)).participants).toEqual([
      'lost',
      'won',
      'unresolved',
      'unresolved',
    ]);

    // Abandon: the guest left mid-game (the staging case); intake marked them abandoned.
    const abandon = await room(1002);
    await handleEngineJobResult(
      db,
      resultPayload(
        await createVerifyJob(db, abandon),
        verified(['won', 'lost', 'won', 'won'], 1505),
      ),
    );
    expect(await outcomesOf(abandon)).toEqual({
      participants: ['won', 'abandoned', 'unresolved', 'unresolved'],
      teams: ['won', 'lost', 'unresolved', 'unresolved'],
    });

    // Two players tied at the buzzer across alliances: still a draw.
    const tie = await room();
    await handleEngineJobResult(
      db,
      resultPayload(await createVerifyJob(db, tie), verified(['won', 'won', 'won', 'won'])),
    );
    expect((await outcomesOf(tie)).participants).toEqual([
      'draw',
      'draw',
      'unresolved',
      'unresolved',
    ]);
  });

  it('records a four-way shared win in a room as draws', async () => {
    const db = database.db;
    const h1 = await createAccount(db, 'Room1');
    const h2 = await createAccount(db, 'Room2');
    const matchId = await createMatch(
      db,
      [
        { side: 0, accountId: h1 },
        { side: 1, accountId: h2 },
        { side: 2, ai: 'nicowar' },
        { side: 3, ai: 'warrush' },
      ],
      { queueId: null, rated: false },
    );
    const job = await createVerifyJob(db, matchId);
    await handleEngineJobResult(db, resultPayload(job, verified(['won', 'won', 'won', 'won'])));
    const participants = await db
      .selectFrom('match_participants')
      .select('outcome')
      .where('match_id', '=', matchId)
      .orderBy('seat')
      .execute();
    expect(participants.map((p) => p.outcome)).toEqual(['draw', 'draw', 'draw', 'draw']);
  });

  it('counts abandonment as a loss and leaves mutual leave unrated', async () => {
    const db = database.db;
    const quitter = await createAccount(db, 'Quitter');
    const stayer = await createAccount(db, 'Stayer');
    const abandoned = await createMatch(db, [
      { side: 0, accountId: quitter, quitTick: 2000 },
      { side: 1, accountId: stayer },
    ]);
    const job = await createVerifyJob(db, abandoned);
    await handleEngineJobResult(db, resultPayload(job, verified(['unresolved', 'unresolved'])));
    expect(await ratingOf(db, quitter)).toMatchObject({ games: 1, wins: 0 });
    expect(await ratingOf(db, stayer)).toMatchObject({ games: 1, wins: 1 });
    const outcomes = await db
      .selectFrom('match_participants')
      .select(['seat', 'outcome'])
      .where('match_id', '=', abandoned)
      .orderBy('seat')
      .execute();
    expect(outcomes.map((o) => o.outcome)).toEqual(['abandoned', 'won']);

    const before = await ratingOf(db, quitter);
    const mutual = await createMatch(db, [
      { side: 0, accountId: quitter, quitTick: 2000 },
      { side: 1, accountId: stayer, quitTick: 2100 },
    ]);
    const job2 = await createVerifyJob(db, mutual);
    await handleEngineJobResult(db, resultPayload(job2, verified(['unresolved', 'unresolved'])));
    expect(await ratingOf(db, quitter)).toEqual(before);
    const row = await db
      .selectFrom('matches')
      .select(['rating_status', 'rating_note'])
      .where('id', '=', mutual)
      .executeTakeFirstOrThrow();
    expect(row).toEqual({ rating_status: 'unchanged', rating_note: 'mutual_leave' });
  });

  it('previews exactly the change a verified result then applies', async () => {
    const db = database.db;
    const winner = await createAccount(db, 'Preview winner');
    const loser = await createAccount(db, 'Preview loser');
    const seats = [
      { side: 0, accountId: winner },
      { side: 1, ai: 'nicowar' as RatedAi },
      { side: 1, accountId: loser },
    ];
    // A first game gives the winner a settled-looking non-default rating.
    const warmup = await createMatch(db, [
      { side: 0, accountId: winner },
      { side: 1, accountId: loser },
    ]);
    await handleEngineJobResult(
      db,
      resultPayload(await createVerifyJob(db, warmup), verified(['won', 'lost'])),
    );
    const matchId = await createMatch(db, seats);
    const preview = await matchRatingPreview(db, matchId, winner);
    const loserPreview = await matchRatingPreview(db, matchId, loser);
    expect(preview).toMatchObject({ ladder: 'ranked-1v1', provisional: true });
    expect(preview!.ifWon).toBeGreaterThan(preview!.before);
    expect(preview!.ifLost).toBeLessThan(preview!.before);
    // Nothing was created for the AI by previewing.
    expect(
      await db.selectFrom('rating_entities').select('id').where('ai_id', '=', 'nicowar').execute(),
    ).toHaveLength(0);

    await handleEngineJobResult(
      db,
      resultPayload(await createVerifyJob(db, matchId), verified(['won', 'lost', 'lost'])),
    );
    const after = await db
      .selectFrom('match_participants')
      .select(['seat', 'rating_before', 'rating_after'])
      .where('match_id', '=', matchId)
      .orderBy('seat')
      .execute();
    expect(after[0]!.rating_before).toBeCloseTo(preview!.before, 9);
    expect(after[0]!.rating_after).toBeCloseTo(preview!.ifWon, 9);
    expect(after[2]!.rating_after).toBeCloseTo(loserPreview!.ifLost, 9);

    // Rooms, unrated queues and strangers get no preview.
    const room = await createMatch(db, seats, { queueId: null, rated: false });
    expect(await matchRatingPreview(db, room, winner)).toBeUndefined();
    const stranger = await createAccount(db, 'Stranger');
    expect(await matchRatingPreview(db, matchId, stranger)).toBeUndefined();
  });

  it('never rates rooms, unverifiable or diverged results', async () => {
    const db = database.db;
    const x = await createAccount(db, 'X');
    const y = await createAccount(db, 'Y');
    const seats = [
      { side: 0, accountId: x },
      { side: 1, accountId: y },
    ];
    const room = await createMatch(db, seats, { queueId: null, rated: false });
    const roomJob = await createVerifyJob(db, room);
    await handleEngineJobResult(db, resultPayload(roomJob, verified(['won', 'lost'])));

    const lost = await createMatch(db, seats);
    const lostJob = await createVerifyJob(db, lost);
    await handleEngineJobResult(
      db,
      resultPayload(lostJob, { verdict: 'unverifiable', reason: 'no client matched' }),
    );

    const diverged = await createMatch(db, seats);
    const divergedJob = await createVerifyJob(db, diverged);
    await handleEngineJobResult(
      db,
      resultPayload(divergedJob, {
        verdict: 'diverged',
        clients: [1],
        outcome: (verified(['won', 'lost']) as { outcome: VerifiedOutcome }).outcome,
      }),
    );

    expect(await ratingOf(db, x)).toBeUndefined();
    const rows = await db
      .selectFrom('matches')
      .select(['id', 'verification', 'rating_status', 'rating_note', 'desync_flagged'])
      .where('id', 'in', [room, lost, diverged])
      .execute();
    const byId = new Map(rows.map((r) => [r.id, r]));
    expect(byId.get(room)).toMatchObject({ rating_status: 'not_rated', rating_note: 'room' });
    expect(byId.get(lost)).toMatchObject({
      verification: 'unverifiable',
      rating_status: 'unchanged',
    });
    expect(byId.get(diverged)).toMatchObject({
      verification: 'diverged',
      desync_flagged: true,
      rating_status: 'unchanged',
      rating_note: 'verification_diverged',
    });
  });

  it('rates a 2v2 with an AI teammate against the AI entity of the match sim version', async () => {
    const db = database.db;
    const h1 = await createAccount(db, 'H1');
    const h2 = await createAccount(db, 'H2');
    const h3 = await createAccount(db, 'H3');
    const matchId = await createMatch(
      db,
      [
        { side: 0, accountId: h1 },
        { side: 0, ai: 'cortex' },
        { side: 1, accountId: h2 },
        { side: 1, accountId: h3 },
      ],
      { queueId: 'ranked-2v2' },
    );
    const job = await createVerifyJob(db, matchId);
    await handleEngineJobResult(db, resultPayload(job, verified(['lost', 'won', 'lost', 'lost'])));
    const cortex = await db
      .selectFrom('ratings')
      .innerJoin('rating_entities', 'rating_entities.id', 'ratings.entity_id')
      .select([
        'ratings.mu',
        'ratings.sigma',
        'ratings.games',
        'ratings.seed_source',
        'rating_entities.ai_sim_version',
      ])
      .where('rating_entities.ai_id', '=', 'cortex')
      .where('ratings.ladder', '=', 'ranked-2v2')
      .executeTakeFirstOrThrow();
    const seed = aiSeedRating('cortex');
    const expected2v2 = rateSides(
      [
        [{ mu: MU0, sigma: SIGMA0 }, seed],
        [
          { mu: MU0, sigma: SIGMA0 },
          { mu: MU0, sigma: SIGMA0 },
        ],
      ],
      [1, 2],
    );
    const [expectedH1, expectedCortex] = expected2v2[0]!;
    expect(cortex).toMatchObject({
      games: 1,
      ai_sim_version: SIM_A,
      seed_source: 'docs/ai/ratings.md elo 1601',
    });
    expect(cortex.mu).toBeCloseTo(expectedCortex!.mu, 9);
    expect((await ratingOf(db, h1, 'ranked-2v2'))!.mu).toBeCloseTo(expectedH1!.mu, 9);
    expect(await ratingOf(db, h3, 'ranked-2v2')).toMatchObject({ games: 1, wins: 0 });
  });

  it('seeds fresh AI entities for a new sim version instead of carrying ratings over', async () => {
    const db = database.db;
    const human = await createAccount(db, 'Challenger');
    const [maximaA] = await aiLadderRatings(db, ['maxima'], SIM_A, 'ranked-1v1');
    const matchId = await createMatch(db, [
      { side: 0, accountId: human },
      { side: 1, ai: 'maxima' },
    ]);
    const job = await createVerifyJob(db, matchId);
    await handleEngineJobResult(db, resultPayload(job, verified(['won', 'lost'])));
    const [maximaAfter] = await aiLadderRatings(db, ['maxima'], SIM_A, 'ranked-1v1');
    expect(maximaAfter!.mu).toBeLessThan(maximaA!.mu);
    expect(maximaAfter!.games).toBe(1);

    const [maximaB] = await aiLadderRatings(db, ['maxima'], SIM_B, 'ranked-1v1');
    expect(maximaB!.entityId).not.toBe(maximaA!.entityId);
    expect(maximaB).toMatchObject({
      mu: aiSeedRating('maxima').mu,
      sigma: AI_SEED_SIGMA,
      games: 0,
    });
    expect(await ensureAiEntity(db, 'maxima', SIM_B)).toBe(maximaB!.entityId);
  });

  it('sweeps verified matches whose ratings are still pending', async () => {
    const db = database.db;
    const m = await createAccount(db, 'M');
    const n = await createAccount(db, 'N');
    const matchId = await createMatch(db, [
      { side: 0, accountId: m },
      { side: 1, accountId: n },
    ]);
    await db
      .updateTable('matches')
      .set({ verification: 'verified' })
      .where('id', '=', matchId)
      .execute();
    await db
      .insertInto('match_team_stats')
      .values([
        { match_id: matchId, team: 0, outcome: 'won' },
        { match_id: matchId, team: 1, outcome: 'lost' },
      ])
      .execute();
    expect(await applyPendingRatings(db)).toBeGreaterThanOrEqual(1);
    expect(await ratingOf(db, m)).toMatchObject({ games: 1, wins: 1 });
    expect(await applyPendingRatings(db)).toBe(0);
  });
});
