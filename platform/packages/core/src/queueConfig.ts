// Quick-match queue settings (instance.yaml `queues:`) and their defaults.
// The schema lives in instanceConfig.ts; this module fills in what an operator
// left out and checks what JSON Schema cannot (map pool team counts).
// See docs/multiplayer/ratings-and-matchmaking.md.
import type { AiId, GeneratorDescriptor } from '@glob2/protocol';
import type { QueueConfig } from './instanceConfig.ts';

export type QueueMode = QueueConfig['mode'];
export type MapPoolEntry = Omit<GeneratorDescriptor, 'seed'>;

/** Colonies (map teams) and seats per side for each mode. */
export const QUEUE_MODES: Record<QueueMode, { sides: number; seatsPerSide: number }> = {
  '1v1': { sides: 2, seatsPerSide: 1 },
  '2v2': { sides: 2, seatsPerSide: 2 },
};

export function modeSeats(mode: QueueMode): number {
  const shape = QUEUE_MODES[mode];
  return shape.sides * shape.seatsPerSide;
}

/** Every AI the platform rates (docs/ai/ratings.md); `none` is not a player. */
export const RATED_AIS: readonly Exclude<AiId, 'none'>[] = [
  'maxima',
  'cabino',
  'nicowar',
  'cortex',
  'warrush',
  'econo',
  'castor',
  'numbi',
];

/**
 * 128x128 generators whose homes are fair by construction (the `fairness:` tag
 * in each generator's registration; see docs/map-generators/ADDING_A_GENERATOR.md):
 * exact symmetry (Symmetric Arena, Sierpinski Gardens), solved fairness (Even
 * Ground's catchments, Marchland's rope), and the repeated-wedge and
 * stamped-lattice sets. Emoji (a novelty) and The Gauntlet are left out, as
 * they are from the AI rating tournament's compatible pool. Revisions are the
 * current registry's; a pool entry whose revision an engine agent does not
 * serve fails generation for that sim version.
 *
 * `twoSide` lists generators verified to generate 128x128 with 2 colonies and
 * `fourSide` those that also fit 4 colonies (several lattice and wedge maps
 * refuse four homes at this size). Rice Terraces needs `slant: 0` at 128x128.
 */
const FAIR_GENERATORS: readonly {
  id: string;
  revision: number;
  fourColonies: boolean;
  params?: Record<string, number>;
}[] = [
  // exact symmetry and solved fairness
  { id: 'symmetric-arena', revision: 1, fourColonies: true },
  { id: 'sierpinski-gardens', revision: 8, fourColonies: false },
  { id: 'even-ground', revision: 2, fourColonies: true },
  { id: 'marchland', revision: 2, fourColonies: true },
  // repeated wedge
  { id: 'amphitheatre', revision: 3, fourColonies: true },
  { id: 'carousel', revision: 2, fourColonies: true },
  { id: 'city-states', revision: 10, fourColonies: true },
  { id: 'coral', revision: 1, fourColonies: true },
  { id: 'fjord-continent', revision: 13, fourColonies: true },
  { id: 'lava-shield', revision: 5, fourColonies: false },
  { id: 'spider-web', revision: 2, fourColonies: true },
  { id: 'switchbacks', revision: 4, fourColonies: true },
  { id: 'tidal-flats', revision: 6, fourColonies: true },
  // stamped lattice
  { id: 'allotments', revision: 2, fourColonies: true },
  { id: 'bajada', revision: 1, fourColonies: true },
  { id: 'breachable-highlands', revision: 5, fourColonies: false },
  { id: 'caravanserai', revision: 3, fourColonies: false },
  { id: 'drumlin-field', revision: 1, fourColonies: true },
  { id: 'forts', revision: 7, fourColonies: true },
  { id: 'glacis', revision: 3, fourColonies: false },
  { id: 'hedgerow-country', revision: 7, fourColonies: true },
  { id: 'hills', revision: 7, fourColonies: false },
  { id: 'karst-towers', revision: 2, fourColonies: true },
  { id: 'locust', revision: 4, fourColonies: true },
  { id: 'old-growth', revision: 4, fourColonies: true },
  { id: 'plantations', revision: 3, fourColonies: true },
  { id: 'polder', revision: 5, fourColonies: true },
  { id: 'rain-shadow', revision: 4, fourColonies: true },
  { id: 'rice-terraces', revision: 3, fourColonies: false, params: { slant: 0 } },
  { id: 'ring-world', revision: 1, fourColonies: true },
];

/** log2 of the map side: 7 = 128 tiles. */
const MAP_SIZE_LOG2 = 7;
/** Seeded rolls per generated map (as in the AI rating tournament). */
const MAP_CANDIDATES = 5;

/** The default map pool for a mode. */
export function defaultMapPool(mode: QueueMode): MapPoolEntry[] {
  const teams = modeSeats(mode);
  return FAIR_GENERATORS.filter((g) => teams <= 2 || g.fourColonies).map((g) => ({
    generatorId: g.id,
    revision: g.revision,
    params: { width: MAP_SIZE_LOG2, height: MAP_SIZE_LOG2, teams, ...g.params },
    candidates: MAP_CANDIDATES,
    startingUnitLevel: 0,
  }));
}

export interface RatingWindow {
  /** Skill difference (display points) accepted immediately. */
  initial: number;
  /** Added per second of waiting. */
  perSecond: number;
  /** Upper bound. */
  max: number;
}

/** A queue with every default filled in. */
export interface ResolvedQueue {
  id: string;
  name: string;
  mode: QueueMode;
  rated: boolean;
  /** Seconds before empty seats are filled with AIs; undefined = never. */
  aiBackfillSeconds: number | undefined;
  /** Accept prompt length for all-human groups; 0 = start immediately. */
  acceptSeconds: number;
  /** Queue ban after declining or ignoring an accept prompt. */
  declineCooldownSeconds: number;
  /**
   * Soft region preference: how bad a shared relay may be for players to be
   * paired, widening with wait until any region will do. Never excludes a
   * player for good; the chosen relay is always the best available.
   */
  rttPreference: RttPreference;
  /**
   * Opt-in hard cap (undefined = off, the default): groups of two or more
   * whose best relay is slower than this for some member do not form. Only
   * for instances with relays near every player; with it set, a far-away
   * player can be matched only through AI backfill.
   */
  maxRttMs: number | undefined;
  ratingWindow: RatingWindow;
  aiPool: readonly Exclude<AiId, 'none'>[];
  mapPool: MapPoolEntry[];
}

/** Ranked queues require a 10-second accept (user decision, 2026-10-01). */
export const RANKED_ACCEPT_SECONDS = 10;
export const DEFAULT_DECLINE_COOLDOWN_SECONDS = 60;

export interface RttPreference {
  /** Worst round trip (ms) accepted for a pairing at once. */
  initialMs: number;
  /** Added per second of waiting. */
  perSecondMs: number;
  /** After this wait, any region (or no shared region at all) is accepted. */
  anyRegionAfterSeconds: number;
}
export const DEFAULT_RTT_PREFERENCE: RttPreference = {
  initialMs: 100,
  perSecondMs: 5,
  anyRegionAfterSeconds: 30,
};
export const DEFAULT_RATING_WINDOW: RatingWindow = { initial: 100, perSecond: 5, max: 800 };

export class QueueConfigError extends Error {}

export function resolveQueue(queue: QueueConfig): ResolvedQueue {
  const mapPool = queue.mapPool ?? defaultMapPool(queue.mode);
  const teams = modeSeats(queue.mode);
  for (const entry of mapPool) {
    const entryTeams = entry.params['teams'];
    if (entryTeams !== undefined && entryTeams !== teams) {
      throw new QueueConfigError(
        `queue ${queue.id}: map pool entry ${entry.generatorId} has ${entryTeams} teams; mode ${queue.mode} needs ${teams}`,
      );
    }
  }
  const aiPool = queue.aiPool ?? RATED_AIS;
  if (aiPool.length === 0 && queue.aiBackfillSeconds !== undefined) {
    throw new QueueConfigError(`queue ${queue.id}: AI backfill needs a non-empty aiPool`);
  }
  return {
    id: queue.id,
    name: queue.name,
    mode: queue.mode,
    rated: queue.rated,
    aiBackfillSeconds: queue.aiBackfillSeconds,
    acceptSeconds: queue.acceptSeconds ?? (queue.rated ? RANKED_ACCEPT_SECONDS : 0),
    declineCooldownSeconds: queue.declineCooldownSeconds ?? DEFAULT_DECLINE_COOLDOWN_SECONDS,
    rttPreference: { ...DEFAULT_RTT_PREFERENCE, ...queue.rttPreference },
    maxRttMs: queue.maxRttMs,
    ratingWindow: { ...DEFAULT_RATING_WINDOW, ...queue.ratingWindow },
    aiPool,
    mapPool: mapPool.map((entry) => ({
      ...entry,
      params: { ...entry.params, teams },
    })),
  };
}
