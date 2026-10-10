# Resource growth verification

Use these harnesses to check ecology and attribute compute costs. [Resource growth architecture](../architecture/resource-growth.md) explains calculation, publication and save ownership. Ecology comparisons do not establish sustainable harvesting or full-engine throughput.

## Player-free and generated-world checks

`ResourceGrowthBenchmark/player-free*` reconciles stock scans with statistics,
using two passive teams and no players, units, buildings or harvesting. Zero-worker/shared
states and statistics must match. `GLOB2_GROWTH_PLAYER_FREE_OUTPUT` selects a JSON
output path and expands testing from two seeds/64 ticks/32² to 20 seeds/512 ticks/64².
`GLOB2_GROWTH_PLAYER_FREE_DELAY` selects 1–16 ticks (default 8) for this test only.
Reports include 128-tick checkpoints and a separate diagnostic terminal flush that
applies pending proposals without calculating more batches. The immediate reference
calls the retained old growth pass, not the current baseline executable. Snapshot timing,
within-pass feedback and random streams remain different; configured seed stocks,
material rates and variety follow the original business rules. Heavy checksums join
pending work, so these checks establish correctness and ecology, not throughput.

`ResourceGrowthBenchmark/generated landscapes*` exercises the actual River, Swamp,
Crater Lakes and Islands generators with their default controls and two colonies,
on 128²/256² maps. It removes colony entities for a no-harvesting comparison, keeping
the generated terrain and resource placement. Each execution variant loads the same
serialized starting world. Every tick reconciles material stocks and deposit counts
with growth statistics and compares zero-worker/shared heavy checksums. Set
`GLOB2_GROWTH_GENERATED_OUTPUT` to a JSON path to expand from one seed/32 ticks to
20 seeds/512 ticks per generator (`GLOB2_GROWTH_GENERATED_TICKS` can select a longer
1–16384 tick horizon) and save representative generated worlds with their original colonies,
empty-colony inputs and endpoints beside the report. Results include per-material
stocks, deposit counts, checkpoints and a separate terminal-queue flush. These are
no-player ecology measurements, not throughput or sustainable-harvesting tests.

`GLOB2_GROWTH_GENERATED_DELAYS` selects a comma-separated list of delays from 1–16
(default `8`). `GLOB2_GROWTH_GENERATED_WIDE=1` adds Rain Shadow, Old Growth, Braided
River, Fjord Continent, Stone Highlands, Tidal Flats, Canals and Continents, including
512² and rectangular maps. `GLOB2_GROWTH_GENERATED_CASE` selects one generator ID
for independent process execution. `GLOB2_GROWTH_GENERATED_SEED_BEGIN` and
`GLOB2_GROWTH_GENERATED_SEED_END` select an inclusive seed range for sharding
large-map campaigns; default coverage is unchanged. Refused generation seeds are recorded rather
than replaced; a selected case with no successful samples fails.

For comparisons across incompatible save formats, `GLOB2_GROWTH_GENERATED_INPUT`
selects a directory of exported baseline fixtures, one subdirectory per generator
and `fixture-<seed>.json` per world. This mode requires case selection and an output
path. It imports classic terrain vertices, the canonical resource catalog and
exact deposit types, varieties and material stocks, then verifies habitat,
growth permission and ecology rates at every cell for all exported renewable
types. Mismatches fail before stepping. Zero-worker/shared arms load identical native
saves of that imported world; the retained immediate-growth pass is omitted in
this mode. Baseline reference results must come from a separately pinned baseline
executable using full simulation ticks, and every missing fixture must be
reconciled against its generation-failure report. Report both delay-versus-delay
and delay-versus-baseline comparisons: only the former isolates scheduling.



For attribution, set `GLOB2_GROWTH_EXISTING_CAPTURE=1` when running the paired
`ResourceGrowthBenchmark` case. Every variant then captures the full shared
component union once per observation, including the legacy control; new growth
uses that existing handle. This separates the standalone cost of introducing
snapshots from growth on an already captured world. It does not simulate AI worker
contention or retained AI leases, and old/new growth trajectories still differ.
Compare full-engine capture counts, capture time and copied bytes as well; total
shared capture time must not be attributed entirely to growth. Worker compute,
queue residence and owner wait timings overlap and must not be added as if they
were sequential costs.

The opt-in `identical live and snapshot kernel inputs` case in
`ResourceGrowthBenchmark` isolates read access: it runs the same pure kernel over
live and captured views of the same unchanged map, compares ordered proposals and
RNG continuation, then times paired runs. Capture, input setup and RNG construction
are outside timing. Set `GLOB2_GROWTH_KERNEL_OUTPUT` to an output JSON path to retain
samples. This measures kernel access costs; it does not measure capture, delayed
publication, retention or contention with other engine jobs.
