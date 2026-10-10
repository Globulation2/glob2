# Stone highlands

Open grass valleys divided by thin stone ridgelines, joined by passes, with a pond in every basin.
Stone is never used up and players can't clear it, so the passes are the only ground routes for
the whole game.

- **Ridges.** Basin sites spaced about `valley-size` apart are scattered on the torus, and every
  tile takes its nearest site after a periodic warp. A tile becomes ridge when any of its eight
  neighbours has a lower label, so no unit can slip through diagonally; noise thickens some
  stretches to two tiles (45% of the ridge at the default stone amount, which scales that
  share), and enclosed pockets under 40 tiles are filled.
- **Passes.** A random spanning tree of `pass-width` openings, cut mid-ridge, joins every valley;
  `loopiness` opens that share of the remaining shared ridges.
- **Homes and ponds.** Colonies take the roomiest valleys by farthest-point spreading, one per
  valley while they last. Ponds (`pond-size`, a percentage of each valley) then grow in basin
  interiors away from ridges, passes and homes, with two-tile beaches so algae can regrow; valleys
  under 120 tiles get none.
- **Resources.** Identical 1:1 wheat and wood kits beside every home, 2:1 farmland ringing the
  ponds, algae in the water, and fruit groves (four per 128×128 at 100% `fruit-amount`) only in
  valleys no colony starts in (unless `home-valley-fruit`, off by default, lets them grow in home
  valleys too).
  `guaranteeStartingResources` runs with the ridges as protected walls, and any resource left in
  or beside a pass is removed. `validateRequest` rejects maps smaller than four valleys or with
  fewer than 1,024 tiles per colony.
- **Checked, not assumed.** The layout is a pure function of the request, so `validateWorld`
  rebuilds it and checks that every ridge tile holds stone, every pass is open and leads to both
  its valleys, and every colony can walk to colony 0.

## Implementation source

[StoneHighlandsGenerator.cpp](../../src/map/generator/generators/StoneHighlandsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
