# JavaScript profile 1 compatibility corpus

`profile1-initial.game.gz` is a version-125 initial save with two JavaScript
controllers and an omniscient JavaScript map script. It embeds `ai.js` and
`map.js`; no source paths are needed to load it. The map is symmetric-arena
(generator 15), seed 42, dimensions 64×64, two teams; game seed 19.

`profile1-256.checksums.gz` records every simulation tick through tick 256.
The scripts exercise visibility-filtered observations, whole-map queries,
private RNG streams, automatically saved globals, gameplay orders, large trig inputs and
subnormal Math results. These are compatibility fixtures, not benchmarks.
Changing them requires explaining the intended profile or simulation change.

Run `python3 test/check_javascript.py /absolute/path/to/glob2 --output artifacts/js-check`
with a fresh output directory. It compares against the current terrain trace, compares
replays and saves at one and four compute workers, and checks resumed state and
ticks from several saved checkpoints, including scenario-callback boundaries. The saved payload comparison excludes only
the existing MapHeader SHA1, which depends on save history; scripts, RNG state,
entity identities, observations and all simulation bytes must match.

`realistic-profile1-initial.game.gz` and its 256-tick trace use the same map and
seeds, with `ai-realistic.js` on both teams and `map-realistic.js`. The economic
planner reads units, building types and visible/remembered resources, calculates
toroidal distances and weighted resource/health/demand scores, then returns
existing worker orders. The omniscient survey calculates fertility weights,
circular map coordinates and colony distances and persists its results.

The `realistic-{economy,survey}-*.txt` files freeze four callbacks on a focused
32×32 production-engine world. `numeric-corpus.js` covers all exposed Math methods,
parsing/coercion/formatting, IEEE edge cases and fixed-seed vectors; finite numbers
in its golden text use hexadecimal binary64 bits. Non-finite script results are
recorded as observable strings because returned data requires finite numbers.
`global-number-corpus.js` separately retains raw non-finite results and IEEE edge
values in automatic globals. Its generated `global-numbers-profile1.value` artifact records their
snapshot representation and save/load continuation. Snapshots canonicalize NaN
sign and payload, which are not observable through profile 1; signed zero and
both infinity signs remain distinct. This checks the actual persisted numeric
boundary in addition to returned-data formatting.

These additions intentionally expand draft profile 1 coverage. The reproduced
`hypot(1.2154874465220262, 1.8249387819142753)` disagreement now expects pinned bits `40018a97b64ae2d6` on every
platform. No fixture preserves the erroneous platform-dependent result. The
two draft initial saves now embed simple scripts with persistent top-level
variables. Their traces are regenerated because source bytes and automatic
global snapshots participate in checksums. Map, seeds, orders and released
non-JavaScript behavior remain unchanged; numeric and realistic returned-data
goldens are retained.

The draft initial saves use format 125 so they include the released format-124
experiment header before the new scripting identity fields. The header receives
a four-byte empty experiment list; its map offset advances by four bytes, its
minor version becomes 125, and its SHA metadata is refreshed. The production
256-tick traces are regenerated because their aggregate includes the MapHeader
version. Every prior team/entity record must remain identical. Script sources,
map, seeds, RNG, orders and numeric/data goldens remain unchanged. Released
format-124 experiment saves retain their own loader path and receive scripting
identities on load. Only the unpublished JavaScript initial saves and their trace
metadata are regenerated for this layout; released save/replay fixtures are not.

Global regression cases cover live contexts, restore,
aliases/cycles, undefined, signed zero, property attributes and rejection rollback.

`JavaScriptSimulation` runs both 256-tick fixtures through the production Engine,
with real AI callbacks, one/four workers, save/resume at ticks 32 and 128, and
actual replay playback. It retains complete traces, saves and replays. The Python
runner adds realistic continuations at 64, 96 and 224. All complete checks include
the aggregate checksum as well as entity records. Saved continuation checks
adjust only the known MapHeader version contribution when a legacy initial save
is written in the current format: the version fields and team/player counts in
both save headers determine that contribution. All entity bytes and the remaining
aggregate checksum must still match, and the resumed tick range must be complete.
The final save payload comparison still excludes only the history-dependent SHA1.
Replay playback supplies AI
orders, while the live runs prove AI execution and persisted AI state.

The focused conversion fixture primes production AI globals, RNG and orders,
then converts a unit into a previously used team slot. A map callback observes
the stale reference immediately, and `conversion-0.game` saves that boundary
before the session/replay begins. The test compares all 256 per-tick records,
same-platform raw saves/replays at one/four workers, decoded final saves after
loading that immediate boundary, and real replay playback from the converted
world. It runs in the shared corpus without replacing the original frozen traces.

The same fixture/trace is intended for all supported native platforms. Browser
replay verification can use the replay and checksum sidecar generated by this
suite; map callbacks still execute when AI decisions are supplied by the replay.

`released-v123.replay.gz` is a gzip copy of the existing browser replay import
fixture (`browser/tests/fixtures/cross-replay.replay`), recorded for 1,500 ticks
from `games/cross-replay.game.gz`, seed 42, by a format-123 build. The shared
compatibility suite retains this replay as a rejection fixture after the
sixteen-team replay floor moved to 127. It accepts replay version 127 and rejects
versions outside the current acceptance range. It accepts client protocol 50
and rejects adjacent protocols, and loads genuine v88, v108 and v121 saves while
validating newly assigned entity identities. The save
floor remains 58; these available historical fixtures do not cover every format
between that floor and the current version.

`master-v124-experiments.game.gz` is a format-124 save generated by the released
experiment-header implementation with `guard-area-balancing` enabled and game
seed 19. It checks that the loader retains the experiment while assigning legacy
entity identities, that the format-125 round trip retains both, and that 64
subsequent ticks preserve world, team and entity execution. Only the MapHeader
format contribution is excluded when comparing the released and upgraded
aggregate checksums; the experiment and simulation records must match.

The format-127 sixteen-team implementation hashes a larger script-generation
table. `profile1-256-teams16.checksums.gz` and
`realistic-profile1-256-teams16.checksums.gz` pin that aggregate checksum layout.
The original traces remain intact: both checkers compare every historical team,
building and unit record between the original and expanded traces at all 256 ticks.
The expanded traces were captured from Linux execution.

`profile1-256-terrain.checksums.gz` and
`realistic-profile1-256-terrain.checksums.gz` are the current simulation baselines.
Canonical terrain IDs now participate in the map checksum, and property-based
ecology can change subsequent simulation behavior. Both checkers still load the
original version-125 saves, then compare complete current traces, one/four-worker
execution and saved continuations; the native suite also checks replay playback.
The native fixture runner's `--update-fixtures` option rewrites only the terrain
traces. Historical traces, initial saves and numeric/data goldens remain intact.
MapHeader version normalization and the final-save SHA1 exclusion described above
remain unchanged.
