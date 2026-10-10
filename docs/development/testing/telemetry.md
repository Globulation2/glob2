# Telemetry verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Performance telemetry

```sh
scons -j8 release=1 server=0 unit-tests
python3 test/run_tests.py --binary unit --filter 'PerformanceTelemetry/*'
```

The injected-clock harness checks online variance, nested timings, exclusion of sleep and
presentation from work, budgets, jitter, sampling rotation, actor generations, capture
boundaries, and the disabled control. SavegameSafetyHarness additionally checks background
write timing counts and completed/failed/superseded accounting. See
[metric definitions and export records](../performance-telemetry.md).

`MapGeneratorGoldenTest PROFILE --performance` compares all built-in generators with timing
off/on (serialized worlds, outcomes, RNG, and generation telemetry) and emits generation
timing records, including site assignment. Use a disposable HOME and run from the repository.

Scene extraction has an opt-in paired diagnostic in the engine harness:

```sh
GLOB2_SCENE_BENCH=1 build/linux/client/release/test/glob2-engine-tests --test-suite=ScenePerformance
```

It compares synchronous extraction with owner capture and pure preparation on the
same seeded state at 128, 512 and 1024 tiles per side, with 512 units. Five warmups
precede 40 samples for unchanged and changed terrain. CSV rows report median and
p95 microseconds plus snapshot pool capacity, leased payload bytes and cumulative
copied bytes. Three retained Scenes model consumer leases. These are extraction
microbenchmarks, not frame-rate or whole-game speedup measurements; pool payload
accounting excludes registry heaps and allocator overhead. No timing threshold is
used as a test assertion.

The end-to-end client diagnostic compares a forced frame-wide simulation boundary
with routine snapshot input in the same executable:

```sh
python3 test/run_tests.py --binary engine --tag benchmark --filter 'EngineSession/snapshot client frame latency*' --artifacts artifacts/client-latency
```

Each arm runs five seconds on `balanced.map`, seed 123, maximum simulation speed,
two compute threads and an 800×600 portable graphics context. The CSV records
completed frames, ticks, input and whole-frame median/p95 duration, and p95 Scene
age. Whole-frame duration includes drawing; input duration excludes it. These are
host processing times, not event-to-photon latency. The forced boundary isolates
parking cost within this implementation, not the performance of an older binary.
Repeat runs with recorded CPU affinity and system load; throughput and frame age
can trade off under contention, and no performance threshold is asserted.

### Distributed gameplay, AI and performance telemetry

`python3 test/test_distributed_game_telemetry.py` tests typed streaming extraction,
64-bit values, escaped text, dynamic fields, unavailable data, malformed records,
compressed artifacts, checksum enforcement and JSONL/CSV roundtrips.
`python3 test/distributed_game_telemetry_integration.py --hosts HOSTS.json --output NEW_DIR`
runs all eight AIs on supplied registered bundles through real workers and the
coordinator. It checks log transfer, complete final telemetry, offline record
counts, export-on/off per-tick checksums, and repeated save/load telemetry
continuation. Host entries need absolute `bundle` paths; workers are stopped after
collection. See [tournament telemetry](../../tools/tournaments.md#gameplay-ai-and-performance-telemetry).
