import type { StudioSettings } from '@glob2/protocol';
// Versioned minimum contract, not a competitive-balance claim. The report is produced
// after native legalization/settlement; never validate only the provider's pixels.
export const PLAYABILITY_VERSION = 'ai-playable-v1';
const finite = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);
export function validatePlayability(raw: unknown, settings: StudioSettings) {
  const report = raw as {
    schema_version: number;
    map: {
      width: number;
      height: number;
      player_slots: number;
      colonies: { alive: boolean; start: { x: number | null; y: number | null } }[];
    };
    space: { growth_disabled: { tiles: number } };
    movement: {
      walking: {
        unreachable_directed_pairs: number;
        colonies: {
          catchment_build_sites_4x4: number;
          resources: Record<
            string,
            { nearest_gather_cost: number | null; catchment_stored_amount: number }
          >;
        }[];
      };
    };
    canonical_quality: {
      measured: boolean;
      colonies: {
        raw: {
          catchment_fertile_grass_tiles: number;
          catchment_buildable_tiles: number;
          mean_fertility: number;
        };
      }[];
    };
  };
  if (
    report.schema_version !== 2 ||
    report.map.width !== settings.width ||
    report.map.height !== settings.height ||
    report.map.player_slots !== settings.players
  )
    throw new Error('Map report does not match the request.');
  if (
    report.map.colonies.length !== settings.players ||
    report.map.colonies.some(
      (c) =>
        c.alive !== true ||
        !finite(c.start.x) ||
        !finite(c.start.y) ||
        c.start.x < 0 ||
        c.start.x >= settings.width ||
        c.start.y < 0 ||
        c.start.y >= settings.height,
    )
  )
    throw new Error('Every colony needs a valid start.');
  if (report.space.growth_disabled.tiles !== 0)
    throw new Error('Generated maps cannot disable resource growth.');
  const walking = report.movement.walking;
  if (walking.unreachable_directed_pairs !== 0 || walking.colonies.length !== settings.players)
    throw new Error('Colony homes must have connected walking routes.');
  if (
    !report.canonical_quality.measured ||
    report.canonical_quality.colonies.length !== settings.players
  )
    throw new Error('Opening economy could not be assessed.');
  for (let i = 0; i < settings.players; i++) {
    const c = walking.colonies[i],
      q = report.canonical_quality.colonies[i]?.raw;
    if (!c || !q) throw new Error('A colony report is missing.');
    if (
      !finite(c.catchment_build_sites_4x4) ||
      c.catchment_build_sites_4x4 < 16 ||
      !finite(q.catchment_buildable_tiles) ||
      q.catchment_buildable_tiles < 128
    )
      throw new Error('A colony has insufficient nearby building space.');
    for (const [name, minimum] of [
      ['wheat', 64],
      ['wood', 16],
    ] as const) {
      const r = c.resources[name];
      if (
        !r ||
        !finite(r.nearest_gather_cost) ||
        r.nearest_gather_cost > 12 ||
        !finite(r.catchment_stored_amount) ||
        r.catchment_stored_amount < minimum
      )
        throw new Error('A colony lacks accessible starter food or timber.');
    }
    if (
      !finite(q.catchment_fertile_grass_tiles) ||
      q.catchment_fertile_grass_tiles < 64 ||
      !finite(q.mean_fertility) ||
      q.mean_fertility <= 0
    )
      throw new Error('A colony lacks renewable food ground.');
  }
  return { contract: PLAYABILITY_VERSION, passed: true };
}
