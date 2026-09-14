# Performance telemetry validation

Validated locally on macOS arm64 with Apple Clang and `scons release=1 server=0`.
Performance comparisons use 6,000-tick fixtures, alternate paired execution order,
and report the median percentage difference in process CPU time. These observations
are noisy; negative values should not be interpreted as a demonstrated speedup.

| Comparison | Completed pairs | Median CPU-time change |
| --- | ---: | ---: |
| Automatic collection vs disabled, same executable | 3 | +5.81% |
| Performance commit vs AI-only commit, mixed AIs | 3 | -2.87% |
| Both exporting: performance commit vs AI-only commit | 3 | +2.79% |
| AI + performance + batched writes vs pre-AI baseline, Cortex | 3 | +1.56% |

The initial mixed-workload result prompted longer alternating comparisons. An AI-only
Cortex regression also prompted batching of binary AI-sample writes. The batching
preserves scalar bytes exactly, including large counters and chunk boundaries; text
and derived stream behavior remain unchanged. No save-format change was introduced.
The user requested finalization without further benchmark expansion. The sub-1% target
is not established: the final collector-only median is +5.81%, with paired results
+0.48%, +5.81%, and +6.07%. The last pair overlapped a short incremental build.
These raw results are retained rather than being treated as a verified low-overhead bound.

## Correctness and footprint

- Automatic collection and timing-disabled runs both match the retained baseline's
  complete per-tick checksum sidecars over 6,000 mixed-AI ticks; each reports 1,232 orders.
- The collector harness passes moments/variance, nesting, budget and jitter arithmetic,
  sampling rotation, bounded nesting, large values, identity, and capture tests. Only
  four clock reads occur across 128 calls to a sampled hot operation.
- Statistics/save harnesses pass, including packed/scalar AI-byte equivalence, malformed
  fields, exact restoration, simulation continuation, and background write success,
  failure, replacement, and timing counts. Legacy version-84/88 fixtures were also
  verified during the preceding AI implementation.
- All 31 built-in generators produce identical serialized worlds, outcomes, RNG state,
  and generation telemetry with performance collection off/on.
- Software rendering at 640×480 and OpenGL at 1024×768 produce valid timing/jitter/budget
  exports. These are smoke checks, not a cross-renderer determinism claim.
- Collector storage is 26,944 bytes on this platform, independent of match length;
  batched serialization uses a 4 KiB stack buffer. No performance fields enter saves.

## Reproduction and retained artifacts

Build and harness commands are in [the metric documentation](performance-telemetry.md).
The final paired raw measurements are committed in
`test/fixtures/performance-telemetry/paired-results.json`. Larger local artifacts are
retained in `output/performance-telemetry/`:

- `benchmark.py`, `benchmark.json`, and `bench-*.log`: initial 108-trial comparison.
- `followup.py`, `followup.json`: longer investigation before sample-write batching.
- `packed-check.py`, `packed.json`, and `packed-*.log`: comparisons summarized above.
- `final-checks.py`, `packed-checksums.log`, `final-*.replay.checksums`: checksum evidence.
- `collector-final.log`, `packed-stats.log`, `packed-save.log`: focused test results.
- `generator-check.log`, `render-check.py`, `render-*.log`, `validate-export.py`: engine
  generation/rendering checks and quote-aware export validation.
- Retained executables and `manifest.json` identify the exact builds/fixtures used;
  the pre-AI and AI-only binaries/fixtures are in `output/ai-telemetry/`.

Linux/Windows runtime checks, interactive pause/network-stall integration checks, and
maintainer playtesting remain unverified. Pause, uncapped mode, and cadence transitions
are covered by the injected-clock collector tests. GPU execution and audio callback
costs are outside this implementation's measurements.
