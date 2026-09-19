# Diagnostic execution validation

`results.txt` records a native macOS run from the same saved initial state with
the feature disabled and enabled. `commands.json` retains the exact commands
and environment overrides (replace the absolute checkout prefix to rerun).
`initial.game.gz`, `off.checksums.gz`, and `on.checksums.gz` retain the inputs and
complete per-tick traces; compare the decompressed sidecars byte for byte.
Generation used symmetric-arena, map seed 42, 64x64, two teams, game seed 731.
Each measured continuation runs through tick 3000 in an isolated profile.

`samples.txt` retains the economy, worker-time and defence records. The focused
worker fixture checks mutually exclusive activity buckets, totals, combat death
attribution, the disabled path and unchanged simulation checksums. The existing
telemetry compatibility suite also compares retained pre-change traces and a
version-108 save continuation (`tests.txt`).

```sh
scons release=1 server=0 worker-telemetry-test
build/src/WorkerTelemetryTest
env -u GLOB2_TEAM_TIMELINE build/src/WorkerTelemetryTest --disabled
python3 test/check_telemetry_simulation.py build/src/glob2
```

Linux/Windows cross-platform checksum and graphics coverage remains outstanding.
