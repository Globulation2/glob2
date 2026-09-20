# The Last Treeline

`last-treeline` (numeric ID 70, revision 1) is a retreating lake basin whose surviving
woodland is the contested construction resource. Dry outer settlements feed themselves;
a broken wooded shoreline supplies continued building. The open lakebed and outer
plain offer alternate approaches, so losing one gathering front leaves another.

The design uses normal game rules. Wood needs nearby pure water and a favourable
mirrored sand probe to grow, spreads into adjacent grass, and disappears when
harvested. Clearing an isolated grove completely therefore destroys its renewal.
There are no protected seed trees, invisible growth restrictions, or extra resources.

## Geography and opening

Two to eight colonies occupy the outer shore. One productive grove lies between
each neighbouring pair of home guides. The shoreline bends by seed, and its
remnant pools bite into woodland interiors. Sand separates crops from towns,
keeps wheat apart from trees, and leaves the lakebed open even after groves fill.
Two construction shoulders stand outside each woodland's sand margin.

Each home receives 48 finite wood tiles on verified zero-growth ground and a
pond-fed wheat plot. No extra timber is scattered across the outer country.
The 48 harvests are an opening budget, not 48 mature-tree stock multiples:
wood is nongranular, so collecting it clears the tile irrespective of tree size.

The generator accepts 256 and 512 tile sides, including rectangles, and 2–8
colonies. Larger maps retain compact first contact and add outer land for
flanking and construction. Starting sites are randomly dealt to colony indices.
Alliances are a game/lobby choice; the generator request does not carry alliances
and does not automatically group allied starts. Duels, FFA and team games use the
same resource geography.

## Controls

- **Woodland depth:** 12, 14 (default), 16. These control levels map to bank scales
  14, 15 and 16; they are not literal measured widths. Enlarges the contained woodland
  around each remnant pool, changing growing room and clearing effort.
- **Wood amount:** scales the neutral groves' initial tree population. Each keeps
  40 initial renewable tree tiles at zero. The 48 finite home trees are unscaled.
- **Wheat amount:** scales additional home seeds; each home retains 64 renewable
  wheat tiles at zero. Unplanted grass reserves growth room.
- **Stone amount:** each home retains four quarry tiles, plus scaled deposits.
- **Fruit amount:** scales home fruit, with no guaranteed minimum. Fruit is not
  concentrated on the contested timber shoreline.
- **Algae amount:** scales algae in the pools, with no guaranteed minimum.

Resource amounts use the standard 0–300%, 25-point-step controls, default 100%.
Productive capacity caps the number of deposits; the amount controls do not
change the terrain or the rate at which individual trees grow.

## Verification

The world validator checks finished-terrain fertility and crop containment,
opening resource access and construction room, first-woodland access, two woodland
choices per colony, and competing access to each grove. It also checks future
approaches when crop plots fill, two-sided harvesting frontage, and outpost room.

`LastTreelineChecks.h` adds shape/count coverage, explicit refusals, resource
extremes, deterministic telemetry, long growth containment and fault injection.
Target it with `MapGeneratorDefaultsTest PROFILE --treeline-only`; it also runs
as part of the regular defaults harness. `--treeline-profile` compares large-map
generation with Orchard Commons, with and without telemetry.

See [the attached review and measurement record](../artifacts/last-treeline/README.md)
for parameter studies, AI rotations, reproducible saves and profiling. Static access
checks and AI games are not proof of human enjoyment or competitive balance.
