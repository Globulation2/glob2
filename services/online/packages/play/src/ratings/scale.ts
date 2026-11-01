// Rating model constants and the scales the platform shows. Ratings are
// OpenSkill (Weng-Lin, Plackett-Luce model) μ/σ pairs; every option is pinned
// here so a library default change can never silently change a ladder.
// See docs/multiplayer/ratings-and-matchmaking.md for the derivations.
import { ordinal, rate, type Options, type Rating } from 'openskill';
import type { AiId } from '@glob2/protocol';

/** Prior for a new player: μ₀ = 25, σ₀ = 25/3 (OpenSkill's defaults). */
export const MU0 = 25;
export const SIGMA0 = 25 / 3;
/** Performance variability β = σ₀/2. */
export const BETA = 25 / 6;
/** Added to σ before every game so ratings never freeze (σ² += τ²). */
export const TAU = 25 / 300;
/** Conservative estimate: ordinal = μ − Z·σ. */
export const Z = 3;

export const OPENSKILL_OPTIONS: Options = Object.freeze({
  mu: MU0,
  sigma: SIGMA0,
  beta: BETA,
  tau: TAU,
  z: Z,
  limitSigma: false,
});

/**
 * Display points per unit of μ. Between two settled players the
 * Plackett-Luce win probability is 1 / (1 + e^(−Δμ / (√2·β))); Elo's is
 * 1 / (1 + 10^(−ΔR/400)). Equating them gives ΔR = Δμ · 400 / (ln 10 · √2 · β),
 * about 29.48, so display differences read like the Elo differences of
 * docs/ai/ratings.md.
 */
export const DISPLAY_PER_MU = 400 / (Math.LN10 * Math.SQRT2 * BETA);

/** σ above which a rating is provisional (about 10 games against settled opponents). */
export const PROVISIONAL_SIGMA = 5;

/**
 * Ordinal that displays as 1500: an average player (μ₀) at the moment their
 * rating stops being provisional (σ = PROVISIONAL_SIGMA).
 */
export const DISPLAY_CENTRE_ORDINAL = MU0 - Z * PROVISIONAL_SIGMA;
export const DISPLAY_CENTRE = 1500;

/** Displayed rating: the ordinal on a 1500-centred, Elo-like scale. */
export function displayRating(r: Rating): number {
  return DISPLAY_CENTRE + DISPLAY_PER_MU * (ordinal(r, { z: Z }) - DISPLAY_CENTRE_ORDINAL);
}

export function isProvisional(r: Rating): boolean {
  return r.sigma > PROVISIONAL_SIGMA;
}

/**
 * Skill estimate used for matchmaking and AI backfill: μ on the display scale
 * (no uncertainty penalty), so a new player's estimate starts at 1500.
 */
export function matchSkill(r: { mu: number }): number {
  return DISPLAY_CENTRE + DISPLAY_PER_MU * (r.mu - MU0);
}

export const DEFAULT_RATING: Readonly<Rating> = Object.freeze({ mu: MU0, sigma: SIGMA0 });

/**
 * Rates one game between sides. `ranks[i]` is side i's finishing place
 * (1 = winner); equal ranks are a draw. Returns new ratings in input order.
 */
export function rateSides(sides: readonly Rating[][], ranks: readonly number[]): Rating[][] {
  if (sides.length !== ranks.length) throw new Error('one rank per side');
  return rate(
    sides.map((side) => side.map((r) => ({ mu: r.mu, sigma: r.sigma }))),
    { ...OPENSKILL_OPTIONS, rank: [...ranks] },
  );
}

// ------------------------------------------------------------ AI seeds

/**
 * Measured strength of each AI from docs/ai/ratings.md (Bradley-Terry fit,
 * centred at 1500, 400 points = tenfold odds; source d37c0c353).
 */
export const AI_SEED_ELO: Readonly<Record<Exclude<AiId, 'none'>, number>> = Object.freeze({
  maxima: 1873,
  cabino: 1680,
  nicowar: 1653,
  cortex: 1601,
  warrush: 1401,
  econo: 1310,
  castor: 1280,
  numbi: 1204,
});

/**
 * σ for a newly seeded AI entity. The doc's intervals (±15 Elo) measure AI
 * against AI on one revision; how an AI fares against people is unknown, so
 * the seed keeps most of a new player's uncertainty (σ₀ = 8.33) while its μ
 * carries the measured order. 6 is ±177 display points per standard deviation
 * and is provisional (> PROVISIONAL_SIGMA) until the AI has played.
 */
export const AI_SEED_SIGMA = 6;

/**
 * Seed rating for an AI: the doc's Elo mapped to μ with the same scale the
 * display uses (μ = μ₀ + (Elo − 1500) / DISPLAY_PER_MU), so an AI's matchmaking
 * skill starts at exactly its documented Elo.
 */
export function aiSeedRating(ai: Exclude<AiId, 'none'>): Rating & { source: string } {
  const elo = AI_SEED_ELO[ai];
  return {
    mu: MU0 + (elo - DISPLAY_CENTRE) / DISPLAY_PER_MU,
    sigma: AI_SEED_SIGMA,
    source: `docs/ai/ratings.md elo ${elo}`,
  };
}
