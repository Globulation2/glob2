# Entity state compaction decision inventory

Baseline: freshly fetched origin/master 277510840054b1416f2defd7d86fe1431dd0ddc0.
Frozen source: artifacts/state-compaction/source (git archive, with identical
measurement-only additions to WorldSnapshotTest.cpp). Linux x86_64, GCC 15,
pinned native SDL3 family built through scons/sdl3_dependencies.py into
artifacts/state-compaction/native-sdl. The initial cached browser prefix was
incorrect for native linking; its failed builds are excluded. Public SDL/WebP
headers were byte-identical, allowing interim object reuse; final binaries come
from successful standard native-prefix SCons builds. Release: dev_fast=0 release=1 linker=auto,
ordinary compilation (no unity/PCH). Baseline and candidate production source
must not change between paired repetitions. Native timing tolerance chosen before
results: 5% capture/consumer, 2% owner; confidence bounds crossing a limit are
inconclusive, not a speed claim. Final release measurements run after compilation
finishes, with ten alternating baseline/candidate pairs and identical affinity.

| Field | Decision | Initialization / runtime / boundary evidence |
|---|---|---|
| Unit medical/activity/displacement/movement | Direct, Uint8 backing | init/actions/medical transitions use named constants; load readState validates raw Uint32 maxima, displacement parity and movement gap 10 remain rejected; enum arithmetic promotes to int; no exposed wide references |
| Unit action | Direct, Uint8 selector | init and UnitAction select global IDs 0..16; loader retains NB_ABILITY validation; global Abilities remains unchanged; performance indexes and checksums promote numeric values |
| Unit direction | Direct, Uint8 | UnitGeometry tables and masked rotations produce 0..8; initialization sentinel 8; loader signed value validated before cast; no output pointer to direction |
| Unit dx/dy | Deferred, Sint32 | init/movement produce -1..1 and loader validates; map searches and exit APIs take int pointers, including failure output writes; left unchanged rather than introduce adapters without separate failed-output measurements |
| Unit level[17] / constructionLevel | Direct, Uint8 | init/catalog levels 0..3, training validated targets, conversion/tiling copy levels; historical worker migration uses max of two validated values; load validates signed raw values; editor wide pointer binding replaced by explicit byte binding; mixed std::max/clamp calls cast to int |
| Unit learning flags | Mask | init from race, refresh from catalog, load nonzero booleans; runtime setters preserve adjacent bits; historical worker migration and observations use bool accessor; save/checksum retain ability iteration order; initialization clears unused bits |
| Unit service/wide cargo/medical recheck/valid target flags | Remain bool | Existing masks not required for measured unit reduction; optional packing deferred until additional savings/access costs demonstrated |
| Unit destination/carried material | Direct, Sint8 | -1 sentinel; destination ends at FEED=18; cargo material namespace MaterialCount; selection/delivery/conversion/catalog policies preserve namespace; loader validates signed temporaries before cast; shifts/checksums retain int promotion |
| Unit animation clocks | Direct, Uint8 | init/load zero; UnitStats sets 20, UnitMedical sets 8; tick decrements only above zero; renderer arithmetic promotes to int; intentionally absent from save and legacy checksum |
| Unit terrain-health remainder | Direct, Sint16 | init zero; accepted loader range -255..255; exposure rate accumulated in int before /256 and %256; preserves signed debt and HP clamp/evaluation order |
| Unit isDead | Remain Sint32 | Arbitrary signed values accepted and participate in checksum; bool conversion would normalize values |
| Unit speeds/delta/performance/experience/HP/hunger/timers/RNG/generations | Remain wide | Historical and current continuation extremes; no change in limits or movement policy |
| Building lifecycle/result | Direct, Uint8 backing | init/construction transitions named constants; loader validates Uint32 maxima before cast; save/checksum numeric meanings unchanged |
| Building maxUnitInside | Direct, Uint16 | init/update from validated catalog, loadCrossRef accepts 0..65535; rejects signed negative/full-width exceptions before cast; no Sint32 reference exposure; arithmetic promotes to int |
| Building minLevelToFlag / minWorkerLevelToFlag | Direct, Uint8 | init zero, validated orders/scripts, editor max 3; signed loader 0..3 checks before cast; tiling copies values |
| Building priority | Remain Sint32 | Despite ordinary orders -1/0/1, loader accepts unbounded Sint32 and oldPriority preserves it; no new rejection rule introduced |
| Building unitStayRange | Deferred, Sint32 | Loaded signed interpretation accepts 0..32767; editor also exposes wide pointer/max binding; potential two-byte reduction requires separate API migration, not needed for current 16-byte reduction |
| Building identifiers | Deferred, Sint32 | Variant IDs, family IDs, origin IDs and funding IDs have distinct namespaces; origin -1 sentinel and configurable catalogs; no content maximum assumed as bound |
| Building staffing | Remain Sint32 | Most staffing loaders unrestricted signed values; retained continuity fields remain authoritative |
| Building ratio/origin ratios | Deferred, Sint32 | Save range 0..32767 but editor binds raw array pointers and production APIs expose Sint32&; narrowing requires additional setters and consumer migration; builtin origin array outside state counted separately |
| Building percentUsed | Remain Sint32 pending transient proof | Loader range 0..32767 alone is not sufficient: swarmStep increments committed selections before allDone/reset; saved jobs and changed preferences require a transient-bound audit |
| Building unitsFailingRequirements | Remain Uint32 | Loader full Uint32 values, increment-only workload evidence cannot prove a lossless bound |
| Building localMaterials/wishedMaterials | Deferred pending exceptional-path/API work | Fifteen slots remain Sint32; materials alias selects team/private; loadCrossRef restores cached demand. Current master rejects negative loaded private stock (plan's older historical-negative premise does not apply). Full-width positive stock and signed demand still require a lossless exceptional representation before narrowing. No common-path content limit imposed. |
| Building exchange masks | Remain Uint32 | Preserve accepted high bits |
| Building clearing/lock/live-only flags | Deferred | No mask proxies/bitfields; additional layout and access benchmarks needed before retaining optional packing |
| Building HP/production timers/ammunition/RNG/generations | Remain wide | No independent bound proof; computation unchanged |

Compiled x86_64 layout (natural alignment 8):

| Record/object | Baseline | Candidate | Reduction |
|---|---:|---:|---:|
| UnitState | 360 | 264 | 96 (26.7%) |
| UnitView | 384 | 288 | 96 (25.0%) |
| Unit | 440 | 344 | 96 (21.8%) |
| BuildingStateRecord | 352 | 336 | 16 (4.5%) |
| BuildingView | 408 | 392 | 16 (3.9%) |
| Building | 1960 | 1944 | 16 (0.8%) |

Entity payload savings per capture are 96 × units + 16 × buildings. Capture copies
one naturally aligned base record per entity without field translation. Existing
sidecars remain unchanged; no new exceptional allocations or representation
metadata. MemoryMetrics accounts retained capacities and leases independently.

Final native-prefix release builds passed. Final candidate: 249 focused release
cases passed; 15 editor/rendering cases passed; 10 baseline/candidate release
renderer PNGs compare pixel-identically. The initial development selection had
288 passes and one timeout in AITargetTelemetrySave; that case passed on release
in 22 seconds. Ten rotated paired entity-workload runs passed every canonical
save and per-tick trace comparison for all five scenarios.

Entity payload: ordinary 18,816 → 15,488 bytes; unit-heavy 203,136 → 153,728;
building-heavy/mixed/wide 116,736 → 109,568. Nine retained entity buffers amplify
these reductions: retained payload decreased by 29,952 / 444,672 / 64,512 bytes
respectively. Material workloads still use wide inline arrays, so these rows do
not evaluate a compact-material fallback or its break-even point.

Paired capture change estimates range from -0.48% to -5.73%, with broad confidence
intervals. Owner/consumer timings mostly remain inconclusive against the chosen
tolerances; no general speed improvement or proven absence of regression is
claimed. These short workloads measure storage reliably and need longer/noisier
CPU qualification before a performance acceptance claim.

Remaining coverage: affected platform/browser execution, individual unit-only
and building-only runtime stages, sustainable normal-economy unit-heavy fixtures,
Callgrind attribution, GUI whole-frame timings, and material exceptional-path work. No runtime speed improvement is
claimed before paired measurements. No versions changed: this candidate is
intended to preserve computation and accepted persistence/network/replay values.


Additional AI match qualification: ten rotated pairs continue the version-121
Numbi/Castor fixture for 128 warmup + 512 measured ticks with CPU affinity 0.
Final checksums and baseline-generated canonical compressed saves match on every
pair. Run CPU estimate is -1.90%, upper 95% bound +1.92%; setup (-2.25%, upper
+2.65%) and save (+0.58%, upper +3.11%) remain inconclusive at the 2% gate. The
benchmark tool therefore reports a failed overall CPU gate, not a proven runtime
regression. Do not present it as a full performance acceptance pass.

With --compute-threads 1 and 4, baseline and candidate produce byte-identical
world.checksums, game.replay.checksums, game.replay and final.game.gz over 512
advancing AI ticks. This directly compares resulting AI orders through the replay
payload as well as per-tick simulation vectors. Browser, Windows, macOS and mobile
execution were not verified. GCC 13/15 layout checks are not cross-platform
simulation coverage.

Reproduction/evidence:
- Native builds: GLOB2_SDL3_PREFIX=$PWD/artifacts/state-compaction/native-sdl
  python3 tools/dev_build.py dev_fast=0 release=1 -j16 engine-tests
  build/linux/client/release/dev-linker-auto/src/glob2 (also from frozen source).
- test/run_tests.py commands and selected inventory are recorded in
  artifacts/state-compaction/final-release.log, final-release.xml and
  final-inventory.json; final 249 cases passed. Earlier display selection and
  release frame images live under renderer-baseline / renderer-candidate-release.
- run-pairs.py, paired/comparison.json: ten pairs × five scenarios, canonical
  saves and per-tick traces retained per pair.
- tools/memory_benchmark.py commands retained in ai-match-paired/comparison.json.
- worker-commands.json and worker-comparison.json preserve configurations, commands
  and SHA-256 evidence for the one/four-worker runs.
- environment.json records baseline SHA, patch SHA-256, compiler, dependency
  manifest, flags and governor; native-baseline-build.log and
  native-release-build.log show successful standard SCons targets.

No PR was created or marked ready. Material compaction remains deferred and its
required promotion/sidecar/break-even coverage has not been implemented. This
candidate establishes the measured narrowing stage; it does not claim to finish
all stages of the supplied plan.
