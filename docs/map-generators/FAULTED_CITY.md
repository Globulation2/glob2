# The Faulted City

A planned city broken into displaced neighbourhoods. Buildable grass avenues follow the
old street grid; broad sandy fractures form a permanent travel network. Junctions connect
the two. There is no central objective or special starting army: colonies settle suitable
lawns beside the city's existing irrigated gardens.

The source city is drawn once, then sampled through a separate integer translation in
each district of a coarse, warped toroidal tessellation. Source block identities persist
across fractures, so outlines and streets continue at different offsets on opposite sides.
The map fills the torus, including its seams. Empty courtyard shells and reclaimed ruins vary independently of district boundaries.
Markets, irrigated gardens, reservoir service courts and orchards have different layouts.
A three-block civic plaza crosses an interior fault: its two surviving pieces visibly
show the displacement, with arcade, fountain and market variants. At least six masonry
tiles of the plaza must survive in each of two districts.

## Controls

- **Fault displacement:** maximum translation on either axis, 4–14 tiles, default 10.
- **Fault width:** the sandy boundary radius is half this corner width, 8–16, default 12.
  Rasterization includes the boundary centreline; final walkable width is measured separately.
- **Ruin density:** percentage of source blocks assigned ruins, 20–60%, default 35%.
- **Surviving junctions:** two cleared avenue mouths per district plus zero, one or two extras
  (Few, Normal, Many). Normal is the default. Mouths are spread along existing streets;
  they remove local masonry detours. Natural gaps remain, so this is not a count of all
  possible district exits. Each mouth reserves a clear five-by-five tile area.
- **Resource amounts:** ordinary resource percentages scale eligible deposits. Structural
  stone remains even at zero. Every renewable tile of each selected starter wheat/wood plot is seeded;
  the sliders scale other plots and ambient resources. Other crop plots fill to 55%
  of renewable capacity at 100%, then increase to full capacity at 300%. Abundance never spills outside
  plots or onto streets, and decorative resources cannot obstruct junction reservations.

Dimensions are 256 or 512 tiles on each axis, with 1–12 colonies. Unsupported dimensions
are rejected before generation. A particular seed can be refused if it lacks viable starts;
the normal lobby's candidate mechanism can choose another seed. The generator searches
up to eight deterministic city layouts before refusing a request. Layout and starting-site
selection use fixed maximum resource amounts, so resource sliders do not redraw terrain.

## Economy and fairness

Sand caps and dividers contain gardens physically; generated maps never disable growth.
Fertility is calculated on final terrain, after beaches. Stone outlines are permanent
resource deposits and supply ammunition as well as upgrades. They do not block tower fire.
Grass streets can be built upon; sandy fractures cannot. Swimming is optional.

Starts are found in existing lawns and ruined courtyards, with seven-by-seven tile
clearance, and checked as completed colonies. Several dispersed arrangements are compared
using the existing start-quality model. Each starter wheat plot has at least 48 fertile
tiles and three units of summed growth probability; no two colonies share that starter
wheat plot. Completed colonies need at least 12 accessible wheat deposits within 12 steps,
24 within 24 steps from their own starter plot, reachable starter wood within 24 steps,
and six separate reachable four-by-four building footprints outside future crop growth. Each
occupied interior component reaches at least two cleared junctions before entering the
fault network. This is measured fairness, not symmetry. Team
indices are randomly dealt after selecting the sites. AI games and human review remain
necessary to assess positions, logistics and the value of junctions.

## Reproduction and evidence

Build the optimized client and map harnesses with the repository's normal SCons targets.
For a single explicit seed:

```sh
build/src/glob2 --generate-map faulted-city --seed 101 --teams 4 \
  --width 256 --height 256 --output artifacts/faulted-city/101.map \
  --preview artifacts/faulted-city/101.png --json artifacts/faulted-city/101.json
```

Generation telemetry uses `faulted-city.*` for source block roles, district translations,
entrance counts, fault coverage and planted resources, plus the shared `starts.scored.*`
records for completed settlement proposals. It is observational and consumes no RNG draws.

The retained [evidence bundle](../../test/fixtures/faulted-city/README.md) contains maps,
previews, a finished save, every study request, game results, timings, and cross-platform
replays/checksums. Reproduction scripts are included.

- **Reliability:** 3,320/3,320 requests passed on Linux x86_64: 2,000 randomized requests,
  1,032 control cases and 288 size/team/worker cases. Random requests exercised every control
  domain, all four dimensions, 1–12 teams and 1–8 workers. 98% of random requests used the
  first layout; the worst needed six of eight attempts. This is sampled coverage, not an
  exhaustive Cartesian proof. All three plaza designs appeared, and every accepted map
  retained at least two plaza pieces.
- **Controls:** eight paired seeds at every slider value showed increasing fault area,
  ruin masonry, junction counts and resource deposits. Mean absolute district shift rose
  from 2.36 to 7.97 tiles between displacement extremes (individual steps vary stochastically).
  Wheat rose from 345 to 2,736 deposits, wood from 68 to 695; the nonzero floors are the
  starter plots. Stone rose from 3,581 to 3,738, with most masonry structural. Increasing ruin
  density separately raised masonry from 3,514 to 3,834 tiles. Junctions produced exactly
  two, three or four reserved mouths per district; they reduce local detours, not a global
  count of routes.
- **Play:** 16 games of 45,000 ticks covered four seeds, all four start rotations, and
  Nicowar, Cortex, Cabino and Maxima together. Every physical start supported a substantial
  colony with at least one AI. Mean peak/final populations were 55.6/18.5, 47.6/40.1,
  91.1/85.3 and 53.3/24.6 respectively. The finished seed-202 save retains clear streets,
  contained crops and a readable plaza. Iterative independent reviews informed the crop
  access, reclaimed ruins, plaza survival and junction protections.
- **Performance:** an early-exit circulation flood preserves the exact building-room
  predicate. Thirty-two alternating before/after Linux measurements produced byte-identical
  map files, with mean generation-command times falling from 0.634 to 0.570 seconds at
  256²/four teams and 3.431 to 2.843 seconds at 512²/twelve teams. On macOS, six largest-map
  requests averaged 2.483 seconds without telemetry and 2.534 with it; Hedgerow Country
  averaged 1.640 seconds on the same size/team/seed set. Full start scoring remains the
  principal cost. Timings are observations on shared hosts, not CI thresholds.
- **Compatibility:** all 440 pre-existing macOS golden rows were preserved; the eight new
  rows match Linux, including the invalid-size refusal. All 424 Linux rows at matching
  revisions were preserved; existing Linux rows for generators 39 and 52 have older
  revisions and were left untouched. A 4,096-tick identical-map/order replay comparison
  produced byte-identical per-tick checksum sidecars on macOS ARM64 and Linux x86_64.
  That simulation check used the preceding terrain revision; no simulation code changed.
  Windows was not exercised. No saved-state or protocol version changes were made.

### Observed limits

The 0.80 fairness threshold applies to start selection at canonical maximum abundance.
Actual resource settings scored as low as 0.701 (random-study mean 0.957). Guaranteed crop
access and building-room checks still passed, but the generator does not promise a final
0.80 floor at arbitrary resource settings.

Late food/logistics attrition remains significant for Nicowar and Maxima. Their mean
worker-starvation counts were 19.7 and 24.1 per game, versus 8.8 and 24.0 in four
Hedgerow Country reference rotations; some colonies reached zero population with few
combat kills. Successful other AIs at those positions do not establish that the collapses
are solely AI defects. These games establish viable openings, expansion and contact,
not equal competitive outcomes or human enjoyment. Human play and native-speaker review
of the translated labels remain useful follow-up validation.
