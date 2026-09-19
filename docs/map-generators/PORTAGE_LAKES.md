# Portage Lakes

Portage Lakes is natural lake country: crooked lakes, dry wooded ridges,
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
size. Candidate fields must meet seed and productive-fertility floors before being accepted.
Every starter wheat field leaves an unseeded 4×4 opening for an inn and a short
entrance to its edge. Both checkerboard harvesting parities must have an available
3×3 inn site beside planted wheat, and workers must initially reach the court.
On long compact maps the opening faces home and grain is sown around its rim,
so early haulers reveal a usable inn site. Scarce sowing is redistributed around
the court when necessary, without increasing the requested seed budget.
These are opening construction sites: unused courts can grow over naturally.
Their area and entrances are excluded from the productive-fertility floor. Dry
ridge wood remains at every abundance: its zero growth probability is checked
against the completed terrain. No generated tile disables resource growth.
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
  plus up to 64 scaled tiles at 100%. Timber keeps 12 starter tiles plus 12
  scaled tiles. High algae settings can saturate their isolated pools; small stone
  patches change in whole-tile steps. Increasing a resource can select a different
  scored settlement proposal, so whole-map totals need not be strictly monotonic.
  The telemetry separates renewable timber planting from structural forest.

Sides are powers of two from 64 through 512, including rectangles. A 64-tile
side selects compact construction. The colony cap is
`min(12, max(2, width * height / 4096))`; one colony is a practice map.
Seed-dependent geometry failures return a diagnostic rather than a partial map.

## Verification and retained evidence

The new generator is optional, registered as ID 65/revision 1. It changes no
existing generator, simulation rule, save format, replay version or network gate.
The full control domain is exposed in the editor and landscape picker. All 33
translation catalogs include the new name, labels and request diagnostics.

Reproduce a preview and native game from the repository root:

```sh
build/src/glob2 --generate-map portage-lakes --seed 1 \
  --width 256 --height 256 --teams 4 --set workers=8 \
  --output /tmp/portage.map --preview /tmp/portage.png --json /tmp/portage.json
SDL_VIDEODRIVER=dummy build/src/glob2 --run-game --map-file /tmp/portage.map \
  --game-seed 2 --player nicowar --player cortex --player cabino --player maxima \
  --ticks 30000 --telemetry team-timeline --save initial --save final \
  --output-dir /tmp/portage-game
```

Run the focused generation, containment, repeatability and scarcity regressions:

```sh
scons release=1 server=0 -j8 build/src/MapGeneratorDefaultsTest
build/src/MapGeneratorDefaultsTest /tmp/portage-contract-profile --portage-lakes-only
```

Retained evidence lives in `artifacts/portage-lakes/`: exact request/statistics
JSON, native maps/previews, initial/final saves, game telemetry, reviews, profile
samples and benchmark scripts. Historical completed logs and saves are losslessly
compressed as `.gz`; study JSON and game-result summaries remain directly readable.
`REVIEW.md` records the repeated map-review and separate translation-review rounds.
The artifact directory is ignored by Git; preserve it alongside the change when
sharing review evidence.

### Reliability

The final version passed all 3,213 generation checks: 2,000 randomized requests,
777 paired control/default/extreme requests, and 436 size/team/boundary checks.
The randomized cohort covered all 16 ordered map shapes, every registered
control value, supported colony counts 1–12 and worker counts 1–8. No reports
were missing and no seeds were counted twice. All four earlier failing
zero-wheat requests passed after budget-preserving re-sowing. The worst final
random request used 11 of the 24 landscape attempts. This is measured coverage,
not an exhaustive guarantee for every seed/parameter combination.

### Parameter evidence

The paired eight-seed control study measures actual finished maps, with each
setting changed on its own at 256×256/four colonies. Lake elongation 125→300
raises the mean swimming shortcut saving from 50.5 to 74.6 walking steps.
Portage depth 2→8 gives exactly 6→24 structural trees per designated cut.
Extra trails 0→100 raises the mean total trail count from 3 to 6. All five
resource controls increase their directly placed resource; renewable timber is
measured separately from structural forest. Algae reaches pool capacity around
250%; stone has whole-tile plateaus; small one/two-colony maps may lack room for
additional useful trails. See `final-control-analysis.md` and its JSON tables in the
evidence directory for individual steps and seeds.

### AI games and observed limits

The 18 retained final-layout games use Nicowar, Cortex, Cabino and Maxima, with a
30,000-tick limit for rotations on full-size, compact and narrow maps, held-out seeds,
and the default four-worker opening. Full-size seed 1 was played with all four
roster rotations; all colonies grew and established economies. The narrow-map
rotations revealed an undiscovered inn court, which was repaired by placing the
opening toward home and sowing its rim. Cortex then built an inn within
1,024–2,048 ticks in every rotation, and all sixteen colony/AI combinations
had no worker starvation through 15,000 ticks.

This does not establish uniform late-game sustainability. In one narrow-map
rotation Maxima lost 28 workers to starvation and was eliminated at tick 27,361;
Nicowar also suffered late starvation. The 64-tile width constrains renewable
land and some AIs overexpand. Tiny maps can cap population growth. Human play
and competitive balance remain unverified. Swimming training and algae delivery
were observed in actual games; the designated cuts' route benefit is checked
statically, without claiming that an AI deliberately selected those cuts.

### Profiling and compatibility

Sampling identified repeated full-map fertility convolution and route floods.
Local fertility windows preserve the exact growth field; endpoint searches stop
once their target is reached. Fifteen paired requests across five shapes and
three seeds retained identical terrain, resource, space and fertility statistics.
On the same Mac, the 512×512/12-colony seed-1 CPU benchmark fell from 18.90 to
about 3.94 seconds (telemetry off); 256×256/four colonies takes about 0.89 seconds.
Collection raises the largest case to about 4.27 CPU seconds. Full route searches
remain the principal cost. Wall times in the retained profiles include contention
from concurrent work; CPU times are reported here for that reason. Comparable
Hedgerow Country and Drumlin Field measurements are retained, rather than
claiming this heavier four-proposal generator matches their speed.

The same saved four-AI initial game was run for 6,000 ticks on macOS/ARM64 and
Linux/x86-64. The complete simulation-checksum traces were byte-identical
(SHA-256 `6958049590ca7fbc1e90d5f37df099ac70f5234598939aaa9ac220f8d783917a`).
The same 6,000 records also match the uninterrupted game after the trace header,
checking initial-save/load continuity. All 448 golden rows per platform pass,
including the 440 preexisting rows unchanged. No Windows execution was verified.
Translation tests and the strict catalog checker pass after a separate agent's
review; native-speaker review of every language has not occurred.

The pre-integration evidence uses provisional ID 58. Integration assigns ID 65
to preserve the generators already merged into the catalog.
