// Turns a verified match result into a rating decision. Pure: no database.
//
// Rules (docs/multiplayer/ratings-and-matchmaking.md):
// - Sides are alliances (MatchSetup teams[].alliance); ratings need exactly two.
// - A verified winner decides the game: the side with a won team ranks first.
// - With no verified winner (the game was cut short), abandonment decides: the
//   first side whose last human left while an opponent was still playing loses.
// - Both sides leaving within MUTUAL_LEAVE_TICKS of each other is a mutual
//   leave, and an unresolved game nobody abandoned is unresolved: neither
//   changes ratings.

/** Two sides leaving within this many ticks (10 s at 25 ticks/s) left together. */
export const MUTUAL_LEAVE_TICKS = 250;

export type TeamOutcome = 'won' | 'lost' | 'unresolved';

export interface OutcomeParticipant {
  seat: number;
  team: number;
  kind: 'human' | 'ai';
  /** Tick the player quit (relay report); undefined when they stayed to the end. */
  quitTick?: number | undefined;
  /** Set when match intake already classified the player as having abandoned. */
  abandoned?: boolean | undefined;
}

export interface OutcomeInput {
  /** alliance of each map team, by team index (MatchSetup teams[]). */
  teamAlliance: ReadonlyMap<number, number>;
  participants: readonly OutcomeParticipant[];
  /** Verified outcome of each team (verify-match). */
  teamOutcomes: ReadonlyMap<number, TeamOutcome>;
  finalTick: number;
}

export type RatingDecision =
  | {
      kind: 'rate';
      /** Alliance numbers, in the order of `ranks`. */
      sides: number[];
      /** 1 = winner, 2 = loser. */
      ranks: number[];
      reason: 'verified' | 'abandoned';
      /** Alliance that abandoned (reason = abandoned). */
      abandonedSide?: number;
    }
  | { kind: 'unchanged'; reason: 'unresolved' | 'mutual_leave' | 'not_two_sides' };

function sideOf(input: OutcomeInput, team: number): number {
  const alliance = input.teamAlliance.get(team);
  if (alliance === undefined) throw new Error(`team ${team} is not in the setup`);
  return alliance;
}

export function decideRating(input: OutcomeInput): RatingDecision {
  const sides = [...new Set(input.participants.map((p) => sideOf(input, p.team)))].sort(
    (a, b) => a - b,
  );
  if (sides.length !== 2) return { kind: 'unchanged', reason: 'not_two_sides' };

  // Verified result per side: won if any of its teams won, lost if all lost.
  const sideResult = new Map<number, TeamOutcome>();
  for (const side of sides) {
    const teams = [...input.teamAlliance].filter(([, a]) => a === side).map(([t]) => t);
    const outcomes = teams.map((t) => input.teamOutcomes.get(t) ?? 'unresolved');
    sideResult.set(
      side,
      outcomes.includes('won')
        ? 'won'
        : outcomes.length > 0 && outcomes.every((o) => o === 'lost')
          ? 'lost'
          : 'unresolved',
    );
  }
  const winners = sides.filter((s) => sideResult.get(s) === 'won');
  if (winners.length === 1) {
    return {
      kind: 'rate',
      sides,
      ranks: sides.map((s) => (s === winners[0] ? 1 : 2)),
      reason: 'verified',
    };
  }
  if (winners.length > 1) return { kind: 'unchanged', reason: 'unresolved' };

  // No verified winner: look at who left. A side left when its last human did;
  // a side with an AI or a human who stayed never left.
  const leftAt = new Map<number, number>();
  for (const side of sides) {
    const members = input.participants.filter((p) => sideOf(input, p.team) === side);
    if (members.some((p) => p.kind === 'ai')) continue;
    const ticks = members.map((p) =>
      p.quitTick !== undefined ? p.quitTick : p.abandoned ? input.finalTick : undefined,
    );
    if (ticks.every((t): t is number => t !== undefined)) leftAt.set(side, Math.max(...ticks));
  }
  if (leftAt.size === 0) return { kind: 'unchanged', reason: 'unresolved' };
  const [first, second] = [...leftAt].sort((a, b) => a[1] - b[1]);
  if (!first) return { kind: 'unchanged', reason: 'unresolved' };
  if (second && second[1] - first[1] <= MUTUAL_LEAVE_TICKS) {
    return { kind: 'unchanged', reason: 'mutual_leave' };
  }
  return {
    kind: 'rate',
    sides,
    ranks: sides.map((s) => (s === first[0] ? 2 : 1)),
    reason: 'abandoned',
    abandonedSide: first[0],
  };
}
