# Save and file compatibility verification

Retained continuation, decoding and serialization regressions. Build and isolate cases through the [native test guide](../../../test/README.md).

## Team capacity and format 127

`TeamLimit` checks all sixteen controller/header slots, entity identifiers, packed
resource-growth attribution, full-array enemy iteration, indexed text alliances,
dense-map request boundaries, malformed script-generation counts, and
deterministic sixteen-team save/load continuation. `Maxima.Economy` covers counted opponents,
legacy twelve-record loading and malformed counts. Its producer-retirement case
also distinguishes a feeding-priority allocation shortfall from an unreachable
site, checks that viable capacity is retained, and verifies unchanged birth
funding. `Maxima.FoodLedger` checks that the corresponding uncontested coverage
signal never doubles resource claims, including mixed-service providers and
zero-demand consumers, and that non-producing services retain their allocation-based
signal. Replay and network boundaries
remain covered by `JavaScriptCompatibility` and `TeamStatsSave`.

`fixtures/team-limit/pre-v127-maxima.game.gz` is an actual format-126 tick-zero
save: Even Ground (method 60), map/game seed 7, 256×256, four colonies with
Maxima controllers and default generator controls. It checks both Maxima records
and the unit/building generation-plane migration, compares serialized AI state,
and advances paired continuations through 128 actual AI decisions and simulation
ticks after a new-format reload. Each continuation owns its RNG snapshot.

The five expanded designed generators retain golden cases at 13–16 colonies on
512×512. Refresh selected landscapes on the current platform with
`MapGeneratorGoldenTest <profile> --update --only=gauntlet,encircled-kingdom,faulted-city,portage-lakes,hungry-marches`.
`--require-rows` requires the current revision of every registered generator on
the executing platform; fresh foreign rows cannot substitute for local coverage.
`MapGeneratorGoldenCoverageTest` covers stale, missing and mixed-revision tables.
Regenerate each affected platform's rows using its actual binary.

## Entering unit save regression

The `EnteringUnitSave` suite (`python3 test/run_tests.py --filter 'EnteringUnitSave/*'`)
links the real engine and
round-trips generated fixtures through binary saved games. It exercises eight
entry directions at five interior/edge/corner positions, preserves the building
reference and animation destination, and rejects both a misplaced entering
explorer and stale occupancy for an ordinary explorer. It protects the runtime
fix in `4ce1d5bc`; expected negative controls print integrity diagnostics.

It needs no display, AI tournament tooling, or external save files. Linux CI runs
it on both supported Ubuntu versions.

Saved state and step-by-step before/after reproduction: [PR #166 fixture](../../../test/fixtures/entering-explorer/README.md).

## Team statistics save compatibility

```sh
python3 test/run_tests.py --filter 'TeamStatsSave/*'
```

The two legacy cases inflate the gzip-compressed fixtures (`test/fixtures/team-stats/version88.game.gz`,
`games/gd-small-2ai.game.gz`) so they exercise loading a genuinely raw legacy save, and
compare the printed trace with `version88.expected.txt` / `version84.expected.txt`
(`--update-fixtures` rewrites them).

This headless test verifies live statistics and smoothing across all 32 sampling
positions and repeated binary reloads, with history-ring wrap, named text fields,
invalid-index and truncated-field controls. It compares version-84 and version-88
save traces against outputs from the original loader. Linux and Windows CI run
it in disposable profiles and check that preferences remain unchanged.
See [fixtures and reproduction steps](../../../test/fixtures/team-stats/README.md).

The harness also covers [gameplay measurements](../../ai/gameplay-statistics.md):
real production, resource, damage, death, treatment, construction and training
paths; 64-bit totals; timestamped coverage; pending projectile/death attribution;
and malformed new fields. `SavegameSafetyHarness` compares measurement totals
through 700 engine ticks after reload, crossing a history sample.

Optional UI artifacts (a `[display]` case, so xvfb or a real display):

```sh
python3 test/run_tests.py --filter 'TeamStatsSave/measurement screenshots*'
```


## Engine save continuation

The `UnitContinuation` suite (`python3 test/run_tests.py --filter 'UnitContinuation/*'`):
five checkpoints compare 256 subsequent
simulation ticks and the RNG state, including idle timers, clearing reservations,
service-list ordering, building worker membership and a nonzero construction
cooldown. Format 114 preserves that cooldown; older formats remain readable.
Linux and Windows CI run
this harness. New saved games preserve live state without running building updates
during load; legacy formats keep their historical reconstruction path.

For full games, compare an uninterrupted sidecar with one or more resumed traces:

```sh
python3 test/compare_save_continuation.py uninterrupted/game.replay.checksums \
  resumed/game.replay.checksums
```

The comparator checks every consecutive team/entity record, reports the first
mismatch, rejects missing/truncated records, and excludes the aggregate checksum
because it includes the save header/version. Run the retained late-game regression
with `python3 test/maxima/check_save_continuation_fixture.py build/native-tests/src/glob2`.

The retained Maxima format-115 checkpoint compares all 512 ticks from 30000
through 30511 against the current terrain simulation's complete-record hashes.
Its midpoint reload also compares complete records, adjusting only the known
save-format contribution to the aggregate checksum. Its compressed save, expected
per-tick hashes, and reproduction commands are in
[maxima/fixtures/save-continuation](../../../test/maxima/fixtures/save-continuation/README.md).

## Untrusted file regression coverage

`UntrustedFiles` mutates serialized entity types, levels and identities, terrain
resources/occupants, sector dimensions and SGSL resume points. It also checks the
compressed-input size limit, the legacy twenty-byte create-order boundary, USL
file-loading denial, unsafe output filenames, AI tags/counts/nesting, network
queue indices and invalid replay order references. Every native AI also loads
its initial saved state under checked reads. `ReplayStepCounter`
checks every truncated prefix of a replay body and step-total overflow;
`USLCoverage` checks native argument type errors, invalid calls, arithmetic
overflow, excessive syntax nesting and runtime recursion limits. Run these alongside
`SavegameSafety`, `LegacyScriptCoverage` and `TeamLimit` for continuation and
legacy compatibility. These targeted tests are not an exhaustive fuzz campaign.


## Memory representation compatibility

`MaximaContinuation` compares compact distance fields and food source masks against
the legacy binary and text encodings, including infinity and maximum finite values.
`Maxima.Farming` checks the box sums against wide reference arithmetic, including
maximum-density maps. `TeamStatsSave` compares compact overlap counters against a
wide oracle on minimum-size tori with 1024 overlapping anchors and replacement of
an entire generation. `PathGradient` checks shared water snapshots, classification
invalidation and frozen readers; `GradientPipeline` checks job lifetime and scheduling.
`SavegameSafety` compares chunked streams against the contiguous backend across
block boundaries, gaps, seeks, zero-length writes and overreads; verifies moved
ownership and byte-capacity bounds; and compares deferred SHA1 and exact gzip bytes
for incompressible input and compression levels zero, one, six and nine. It rejects
truncated, bad-CRC, trailing and concatenated gzip inputs before game loading,
injects allocation/finalization exceptions, checks atomic failure cleanup, and
stalls an autosave writer to verify nonblocking deferral and exact saved ticks
at the later capture. It compares owned snapshot output against ordinary saves
after mutating the live game, and exercises both native-thread and cooperative
finalization, including failure and subsequent reuse. It checks mixed snapshot/legacy
queues, exact cooperative gzip bytes at the default compression level, and manual save
ordering through delayed capture and persistence, including terminal failures and
exactly-once success callbacks. The optional level-zero compatibility path retains whole buffers;
normal save-memory measurements use the default compression level.

Run these alongside the existing placement, continuation and engine lifecycle suites.
For full-game checks, retain identical initial saves, seeds and orders, compare
per-tick simulation state and replay/save bytes, and test continuation from populated
checkpoints. The native paired CPU runner and profiling workflow are described in
[the development reference](../memory-benchmarks.md#native-simulation-memory-and-cpu-comparisons).

## Save size measurements

Build `scons release=1 server=0 save-size-harness`, then run:

```sh
python3 test/measure_save_sizes.py build/darwin/client/release/test/SaveSizeHarness \
  artifacts/save-size/baseline maps/SmallForTwo.map.gz maps/Holiday_Island_2.map.gz \
  games/gd-small-2ai.game.gz games/gd-large-4ai.game.gz
```

Use the native build directory for the host (or the explicit custom build path).
Repeat with the comparison revision's harness, the same input files and a different
output directory. Each invocation uses disposable profiles, retains gzip outputs
and JSON section sizes, and reports median load/serialization/compression times
from three processes. Input and harness hashes identify the measured inputs.
Peak RSS is the process high-water mark through serialization/compression, including
loading; it is not isolated serializer allocation. Independently compressed section
sizes do not add up exactly to the final gzip size. Run the harness on its emitted
files as well when comparing load times for the old and new encodings. Keep timing
runs separate from concurrent builds/tests and compare matching compilers and flags.

`PackedArray` checks all integer widths, wraparound, block boundaries and malformed
payloads. `Maxima.Continuation` covers legacy and compact arrays, signed limits and
nested archives. `Maxima.Placement` retains explicit noncanonical neighborhood
contents. `TeamStatsSave` checks binary measurement/end-game histories and binary/text
telemetry histories across two 256-sample batch boundaries;
`TeamLimit`, `JavaScriptCompatibility`, `UntrustedFiles` and `SavegameSafety` cover
sparse identities, format boundaries, decoded validation and save/load continuation.
Run these together with the existing AI and gradient continuation suites. An encoding
change must preserve decoded state and per-tick simulation records; old and new
serialized bytes and header checksums are expected to differ.

For interactive snapshot capture/finalization measurements, set
`GLOB2_BENCH_SNAPSHOT=1` when running `test/measure_save_sizes.py`. The report
separates `capture_ms` from `encode_ms`; `serialize_ms` is their sum. Compare with
ordinary serialization using the same fixtures and build. Capture timing includes
the immutable copy and its lightweight in-memory representation; encoding includes
final array/history packing, offset relocation and hashing. The benchmark flattens
the finished output for section-independent measurement, so its process peak is
not an isolated allocation bound for the production writer.
