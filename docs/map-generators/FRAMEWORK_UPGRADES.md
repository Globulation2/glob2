# Map generator framework capabilities

## Generalized capabilities

| Requirement observed in the prototypes | Framework support | Responsibility retained by a future generator |
|---|---|---|
| Literal cell sizes and odd corridor widths | `GeneratorControl::allowedValues`, domain indexing, centralized normalization/display and catalog export | Named options, domains and tuned defaults |
| Rooms fitting cells, dimensions divisible by a cell size | Pure `validateRequest` callback, consumed by service, lobby and editor | Cross-control and dimension constraints; no silent settings rewrites |
| Jagged islands and stretched/rotated continents | `shared/Geometry`: invertible shape transform, seeded radial shape, conservative radius bound, label-grid rasterization | Coastline recipe, fjord paths, moat geometry and bridge endpoints |
| Joint dispersion of homes and shared objectives | Optional whole-region search in `Regions`, paired weight shuffling, convergence and evaluation budgets | Weight policy and checks on actual separation; best-response is not an equal-distance or global-optimum guarantee |
| Distances through a maze, connected peninsulas, neighboring resource zones | `shared/Topology`: graph distances, grid components with explicit wrapping/neighbor policy, sparse-label adjacency | Graph construction, corridor carving and resource-role assignment |
| Settlements restricted to a home island or room | `shared/Settlements`: whole-footprint mask, nearest legal anchor, exact worker count, per-colony diagnostics | Home mask, anchor choice and when to place starter resources |
| Generator-specific connectivity guarantees | Optional `validateWorld` callback after structural checks | Checking actual finished terrain or movement masks, with useful error detail |

All helpers are ordinary functions or small value objects. There is no new generator superclass, pipeline dispatcher, terrain repair policy or serialization extension. Geometry and topology operate on dimensions and grids, independently of `Game`. Settlements use `Game` because buildings and units require its mutation APIs.

## Bugs and safeguards

- The point splitter previously shuffled points without weights. It now moves both together. Existing callers use uniform weights, so their sampled default outputs are unchanged.
- Whole-region dispersion is opt-in. It fails explicitly after the configured pass limit or 20 million distance evaluations rather than silently treating an unfinished search as convergence. Invalid weight arrays and insufficient region capacity are rejected. A single site uses half the shorter map dimension as its finite spacing budget.
- The prototypes multiply coastline amplitudes by as much as 1.4 but budget space using `radius * (1 + roughness)`. The new radial primitive bounds the amplitude sum and exposes its actual conservative maximum radius. Future moat/outlier placement must include the transform's stretch and both shapes' extents.
- The prototype settlement helper can succeed with fewer workers than requested. The shared helper fails with the requested/available count and the service retains its exact-count structural check. The entire swarm footprint must fit inside the home mask.
- The editor previously placed every Layout control at the same vertical position. Controls now occupy independent rows. Literal discrete values and exponent display use the same metadata in both UIs.
- Ellipse rasterization used 32-bit products that can overflow at supported large dimensions. Intermediate arithmetic is now 64-bit. Resource patch sizes are checked before modulo operations and engine assertions.
- Lattice and the prototype maze placed grass directly beside water. Those undermap combinations render as grass in the terrain lookup, so the apparent water pattern was not gameplay water. Both modules now budget shoreline width before calling `controlSand()` and expose dimensions for the finished terrain.
- Maze uses 20, 24 or 32-tile cells, larger home rooms and compact starter deposits. Closed graph edges carry a continuous one-tile stone seam on grass with sand and water on both sides, while connected edges remain open passages.
- Fjord deposits wheat and wood in compact patches on both banks of every fjord. Bank deposits are placed last and shared clump placement never overwrites another resource type. Single-colony maps skip the vacuous bank requirement.
- Lattice uses larger home clearings and puts wheat and wood on opposite rims. Its tiny repeating islets remain the dominant visual pattern without starving the colonies of construction space.

The generators still validate their actual construction results. A moat must connect to land at both bridge ends, jagged outlines must leave legal settlement footprints, and the fjord core must keep every player peninsula connected. Difficult small/crowded combinations may fail, but return reproducible stage diagnostics and are discarded by the lifecycle service.
