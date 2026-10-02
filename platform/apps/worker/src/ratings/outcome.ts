// Turns a verified match result into a rating decision. Pure: no database.
//
// Rules (docs/multiplayer/ratings-and-matchmaking.md):
// - Sides are alliances (MatchSetup teams[].alliance); ratings need exactly two.
// - A verified winner decides the game: the side with a won team ranks first.
// - More than one side with a won team is a shared win, which the platform
//   records as a draw (participantOutcomes) and does not rate. The engine
//   reports every team tied for the most prestige as won when the prestige
//   goal is reached or the sudden-death timer runs out, exactly as the in-game
//   end screen tells each of those players "you have won".
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
  /**
   * Outcome of each team: the verifier's (verify-match), or as recorded by
   * participantOutcomes, where a shared win reads 'draw'.
   */
  teamOutcomes: ReadonlyMap<number, RecordedOutcome>;
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
  | { kind: 'unchanged'; reason: 'unresolved' | 'draw' | 'mutual_leave' | 'not_two_sides' };

/** A participant's or team's recorded result: a verified team outcome, or a shared win as a draw. */
export type RecordedOutcome = 'won' | 'lost' | 'draw' | 'unresolved';

/**
 * Maps the verifier's per-team outcomes to the outcomes the platform records
 * for teams and participants. The engine marks every team that met a winning
 * condition as won, and ties count: a prestige or sudden-death tie at the top
 * makes each tied team a winner, allied or not. A win is only a win when one
 * side (alliance) holds it; when teams of two or more sides won, each of
 * those teams drew. Lost and unresolved teams keep their outcome.
 */
export function participantOutcomes(
  teamAlliance: ReadonlyMap<number, number>,
  teamOutcomes: ReadonlyMap<number, TeamOutcome>,
): Map<number, RecordedOutcome> {
  const winningSides = new Set<number>();
  for (const [team, outcome] of teamOutcomes) {
    if (outcome !== 'won') continue;
    // A team missing from the setup is its own side.
    winningSides.add(teamAlliance.get(team) ?? -1 - team);
  }
  const shared = winningSides.size > 1;
  return new Map(
    [...teamOutcomes].map(([team, outcome]) => [
      team,
      shared && outcome === 'won' ? 'draw' : outcome,
    ]),
  );
}

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

  // Verified result per side: won if any of its teams won (drew if any drew),
  // lost if all lost.
  const sideResult = new Map<number, RecordedOutcome>();
  for (const side of sides) {
    const teams = [...input.teamAlliance].filter(([, a]) => a === side).map(([t]) => t);
    const outcomes = teams.map((t) => input.teamOutcomes.get(t) ?? 'unresolved');
    sideResult.set(
      side,
      outcomes.includes('won')
        ? 'won'
        : outcomes.includes('draw')
          ? 'draw'
          : outcomes.length > 0 && outcomes.every((o) => o === 'lost')
            ? 'lost'
            : 'unresolved',
    );
  }
  const winners = sides.filter((s) => sideResult.get(s) === 'won');
  // Both sides won (a prestige or sudden-death tie at the top), or the
  // recorded outcomes already say draw: a draw, which is not rated.
  if (winners.length > 1 || sides.some((s) => sideResult.get(s) === 'draw')) {
    return { kind: 'unchanged', reason: 'draw' };
  }
  if (winners.length === 1) {
    return {
      kind: 'rate',
      sides,
      ranks: sides.map((s) => (s === winners[0] ? 1 : 2)),
      reason: 'verified',
    };
  }
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
