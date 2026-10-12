# Portage Lakes

Portage Lakes (ID 65) is natural lake country: crooked lakes, dry wooded ridges,
pale trails and farms in sheltered bays. Its two investments change different
connections. Cutting opens a public land shortcut through a ridge; swimming
opens a crossing between opposite shores. Initial land routes connect every
colony without either investment.

## Design contract

Starts are found beside existing water, not stamped from identical home plots.
Four complete settlement proposals are compared using the shared start scorer.
If none works, a bounded search tries up to 24 landscapes. The final validator
reconstructs the selected design from the request, ordered starts and exact terrain.
This is measured asymmetric fairness, not a promise of identical opportunities.
The ordinary swarm and requested workers must have renewable food, timber,
quarry access and building room. Portages belong to expansion ground rather
than the opening town.

Wheat and renewable timber occupy irregular sand-contained shore plots. Full-size
wheat fields are larger than compact fields; crowded maps use an intermediate
size. Their footprints stay fixed while sowing uses more of the available ground. Candidate fields must meet seed and productive-fertility floors before being accepted.
Each substantial lake also receives additional shore wheat fields: two where
space permits on full maps, with at least one per lake; compact maps receive one.
Full maps budget the first field after home farms; additional fields fit around
neutral bays, roads and landing clearings once those are secured. Compact maps
place their field after the landing clearings, which have less room to move.
They may clear ordinary shoreline woodland while preserving the designated
portage plugs. These fields use smaller footprints where the shore is tight and
scale their sowing with wheat amount; they are empty at 0%. No new water is added.
Every substantial wheat field leaves an unseeded 4×4 opening for an inn and a short
entrance to its edge. Both checkerboard harvesting parities must have an available
3×3 inn site beside planted wheat, and workers must initially reach the court.
Starter-field openings face home and grain is sown around the court rim,
so early haulers reveal a usable inn site. Scarce sowing is redistributed around
the court when necessary, without increasing the requested seed budget.
These are opening construction sites: unused courts can grow over naturally.
Their area and entrances are excluded from the productive-fertility floor. Dry
ridge wood remains at every abundance: its zero growth probability is checked
against the completed terrain. No generated tile disables resource growth.
Additional fields directly follow existing lake shores, with only the beach
margin between grain and water. They use smooth shore-aligned outlines, never
per-tile random thinning. Accepted fields must have at least six productive tiles
within three steps of pure water and two tile-equivalents of exact growth
potential outside their inn court and entrance. Dry scattered wheat is not used
as a substitute for renewable farmland. Trails, landings and portage plugs remain
protected.
Algae occupies separate small pools, so the main swimming lakes remain clear.
Stone outcrops shape some woodland crossings without enclosing every clearing.
Full maps guarantee two designated portages; the largest maps target up to four
when useful crossings and clearings fit. Compact maps guarantee one. Every portage saves a measured walk
even after swimming and the other designated cuts are available. Each landing
connects to the initial colony network and has space for a building.
The large lakes remain separate: with multiple lakes, no connected water component
may contain more than 70% of their water.

## Controls and size policy

- **Lake elongation:** 125–300%, step 25, default 200. Longer, narrower lakes
  increase the difference between a shore walk and crossing the water.
- **Portage depth:** 2–8 tree rows, default 4. Sets the structural cutting depth.
- **Extra trails:** 0–100%, step 25, default 25. Additional initially open land
  routes beyond the network needed to connect settlements. One/two-colony maps
  instead offer optional approaches to neutral shoreline bays. Coarse steps avoid
  false precision when the settlement graph has only a few spare connections.
- **Resource amounts:** standard 0–300% controls. Starting supplies, structural
  woodland and structural rock are independent guarantees. Renewable plots
  saturate at their available fertile area without invading roads or towns.
  Wheat keeps 20 starter tiles per compact colony or 32 per full-size colony,
  plus up to 96 compact or 128 full-size scaled tiles at 100%. Timber keeps 12 starter tiles plus 12
  scaled tiles. High algae settings can saturate their isolated pools; small stone
  patches change in whole-tile steps. Increasing a resource can select a different
  scored settlement proposal, so whole-map totals need not be strictly monotonic.
  The telemetry separates renewable timber planting from structural forest.

Sides are powers of two from 64 through 512, including rectangles. A 64-tile
side selects compact construction. The colony cap is
`min(12, max(2, width * height / 4096))` on smaller shapes, and sixteen
on 512×512. Counts above twelve require that largest square; one colony is a practice map.
Seed-dependent geometry failures return a diagnostic rather than a partial map.

## Playtest limitations

Narrow maps constrain renewable land and can expose late starvation when a controller overexpands. A reachable portage and observed swimming do not prove an AI deliberately used the intended route. Tiny maps can cap growth.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[PortageLakesGenerator.cpp](../../src/map/generator/generators/PortageLakesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
