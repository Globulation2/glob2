import { it, expect } from 'vitest';
import { playabilityChecks, validatePlayability } from '../src/playability.ts';
const settings = { width: 256, height: 128, players: 2 } as const;
function report() {
  return {
    schema_version: 2,
    map: {
      width: 256,
      height: 128,
      player_slots: 2,
      colonies: [0, 1].map(() => ({ alive: true, start: { x: 1, y: 1 } })),
    },
    space: { growth_disabled: { tiles: 0 } },
    movement: {
      walking: {
        unreachable_directed_pairs: 0,
        colonies: [0, 1].map(() => ({
          catchment_build_sites_4x4: 32,
          resources: {
            wheat: { nearest_gather_cost: 8, catchment_stored_amount: 256 },
            wood: { nearest_gather_cost: 8, catchment_stored_amount: 128 },
          },
        })),
      },
    },
    canonical_quality: {
      measured: true,
      colonies: [0, 1].map(() => ({
        raw: {
          catchment_fertile_grass_tiles: 128,
          catchment_buildable_tiles: 256,
          mean_fertility: 100,
        },
      })),
    },
  };
}
it('accepts a connected opening economy in the requested rectangle', () =>
  expect(validatePlayability(report(), settings).passed).toBe(true));
it('rejects unreachable starts, missing food, narrow building space and no-growth zones', () => {
  const disconnected = report();
  disconnected.movement.walking.unreachable_directed_pairs = 2;
  expect(() => validatePlayability(disconnected, settings)).toThrow('walking routes');
  const food = report();
  food.movement.walking.colonies[0]!.resources.wheat.catchment_stored_amount = 0;
  expect(() => validatePlayability(food, settings)).toThrow('food or timber');
  const room = report();
  room.movement.walking.colonies[0]!.catchment_build_sites_4x4 = 1;
  expect(() => validatePlayability(room, settings)).toThrow('building space');
  const growth = report();
  growth.space.growth_disabled.tiles = 1;
  expect(() => validatePlayability(growth, settings)).toThrow('disable');
});

it('fails closed when native economy measurements are missing', () => {
  const incomplete = report();
  delete (
    incomplete.canonical_quality.colonies[0]!.raw as Partial<
      (typeof incomplete.canonical_quality.colonies)[0]['raw']
    >
  ).catchment_buildable_tiles;
  expect(() => validatePlayability(incomplete, settings)).toThrow('building space');
});

it('streams a complete pending checklist followed by running and terminal results', () => {
  const checks = [...playabilityChecks(report(), settings)];
  expect(checks.slice(0, 13).every((check) => check.status === 'pending')).toBe(true);
  expect(checks.slice(13).map((check) => check.status)).toEqual(
    Array.from({ length: 13 }, () => ['running', 'passed']).flat(),
  );
  expect(
    checks.find((check) => check.id === 'building-0' && check.status === 'passed'),
  ).toMatchObject({
    colony: 0,
    location: { x: 1, y: 1 },
  });
});

it('reports independent failures while keeping valid colony measurements visible', () => {
  const raw = report();
  raw.movement.walking.unreachable_directed_pairs = 2;
  raw.movement.walking.colonies[0]!.resources.wheat.catchment_stored_amount = 0;
  raw.space.growth_disabled.tiles = 1;
  const checks = [...playabilityChecks(raw, settings)];
  expect(checks.filter((check) => check.status === 'failed').map((check) => check.id)).toEqual([
    'growth',
    'routes',
    'wheat-0',
  ]);
  expect(checks.find((check) => check.id === 'wood-1' && check.status === 'passed')).toBeDefined();
});

it('does not evaluate dependent economy checks when canonical measurements are unavailable', () => {
  const raw = report();
  raw.canonical_quality.measured = false;
  const checks = [...playabilityChecks(raw, settings)];
  expect(
    checks.filter((check) => check.status === 'not-evaluated').map((check) => check.id),
  ).toEqual(['building-0', 'renewable-0', 'building-1', 'renewable-1']);
  expect(checks.find((check) => check.id === 'wheat-0' && check.status === 'passed')).toBeDefined();
  expect(() => validatePlayability(raw, settings)).toThrow('could not be assessed');
});

it.each([null, undefined, {}, [], true, 'invalid', { schema_version: 2, map: null }])(
  'fails closed with a useful outcome for malformed reports: %j',
  (raw) => {
    const checks = [...playabilityChecks(raw, settings)];
    expect(
      checks.find((check) => check.id === 'request' && check.status === 'failed'),
    ).toBeDefined();
    expect(checks.filter((check) => check.status === 'not-evaluated')).toHaveLength(12);
    expect(() => validatePlayability(raw, settings)).toThrow('does not match');
  },
);

it('keeps every existing minimum boundary, including strictly positive fertility', () => {
  const raw = report();
  for (const colony of raw.movement.walking.colonies) {
    colony.catchment_build_sites_4x4 = 16;
    colony.resources.wheat = { nearest_gather_cost: 12, catchment_stored_amount: 64 };
    colony.resources.wood = { nearest_gather_cost: 12, catchment_stored_amount: 16 };
  }
  for (const colony of raw.canonical_quality.colonies)
    colony.raw = {
      catchment_buildable_tiles: 128,
      catchment_fertile_grass_tiles: 64,
      mean_fertility: Number.MIN_VALUE,
    };
  expect(validatePlayability(raw, settings).passed).toBe(true);
  const mutations: ((r: ReturnType<typeof report>) => void)[] = [
    (r) => {
      r.movement.walking.colonies[0]!.catchment_build_sites_4x4 = 15;
    },
    (r) => {
      r.movement.walking.colonies[0]!.resources.wheat.catchment_stored_amount = 63;
    },
    (r) => {
      r.movement.walking.colonies[0]!.resources.wood.catchment_stored_amount = 15;
    },
    (r) => {
      r.movement.walking.colonies[0]!.resources.wheat.nearest_gather_cost = 13;
    },
    (r) => {
      r.canonical_quality.colonies[0]!.raw.catchment_buildable_tiles = 127;
    },
    (r) => {
      r.canonical_quality.colonies[0]!.raw.catchment_fertile_grass_tiles = 63;
    },
    (r) => {
      r.canonical_quality.colonies[0]!.raw.mean_fertility = 0;
    },
    (r) => {
      r.map.colonies[0]!.start.x = settings.width;
    },
    (r) => {
      r.map.colonies[0]!.start.y = -1;
    },
    (r) => {
      r.canonical_quality.colonies[0]!.raw.mean_fertility = NaN;
    },
    (r) => {
      r.movement.walking.colonies[0]!.resources.wheat.nearest_gather_cost = Infinity;
    },
  ];
  for (const mutate of mutations) {
    const invalid = structuredClone(raw);
    mutate(invalid);
    expect(() => validatePlayability(invalid, settings)).toThrow();
  }
});

it('handles missing nested measurements without TypeErrors or fabricated locations', () => {
  const raw = report();
  (raw.map.colonies[0] as unknown as { start: unknown }).start = null;
  (raw.movement.walking.colonies[0] as unknown as { resources: unknown }).resources = null;
  const checks = [...playabilityChecks(raw, settings)];
  expect(checks.filter((check) => check.status === 'failed').map((check) => check.id)).toEqual([
    'starts',
    'wheat-0',
    'wood-0',
  ]);
  expect(
    checks.find((check) => check.id === 'wheat-0' && check.status === 'failed'),
  ).not.toHaveProperty('location');
});
