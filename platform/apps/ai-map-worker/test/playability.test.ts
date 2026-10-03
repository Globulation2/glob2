import { it, expect } from 'vitest';
import { validatePlayability } from '../src/playability.ts';
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
