# Gauntlet profiling comparison

Independent comparison on macOS 26.6.2 (25G83), Darwin 25.6.0, arm64. The frozen executables differ only in the farm-boundary distance optimization; their SHA-256 identities are retained in [binary-sha256.txt](binary-sha256.txt). Frozen binaries are retained locally under `artifacts/gauntlet/` (ignored by Git); the compact evidence here includes their identities.

## Correctness

[comparison.json](comparison.json) records all 72 requests: seeds 1–12, square sizes 256 and 512, and 4, 8, or 12 colonies. Sixty requests succeeded. All twelve 256×256/12-colony requests refused identically because of layout capacity. Every successful serialized map had identical SHA-256 hashes before and after optimization. All complete JSON reports compared equal with **no fields removed**, including the refusal reports. The compact artifact retains comparison outcomes, hashes, process status, stdout/stderr, and wall times; bulky per-case reports are omitted.

## Timing

Seven alternating-order paired repetitions at 512×512, 12 colonies, seed 1:

| Output | Before median | After median | Median reduction |
| --- | ---: | ---: | ---: |
| Map only | 2.7063 s | 2.6301 s | 2.8% |
| Map and JSON report | 3.1817 s | 2.9973 s | 5.8% |

These are whole-process wall times under substantial shared-machine load, including startup and output work, not isolated function timings. Map-only runs ranged from 2.524–3.893 s before and 2.130–2.991 s after. Treat the performance improvement as modest, noisy evidence; exact output equivalence is the stronger result. Raw repetitions are in [benchmark.json](benchmark.json), aggregate results in [summary.json](summary.json), and progress/output in [run.log](run.log).

## Reproduction

Run from the repository root with asset files available. Supply the two frozen executable versions at the paths expected by [compare.py](compare.py), or change its `base` and `bins` variables. The script writes under `/tmp/gauntlet-evidence/profile-comparison` and compares outputs at identical paths to avoid filename differences in saved maps.

```sh
mkdir -p /tmp/gauntlet-evidence/profile-comparison
python3 docs/artifacts/gauntlet/profile/compare.py > /tmp/gauntlet-evidence/profile-comparison/run.log
```

Each correctness request uses the following command with `BINARY` selected in turn and `SIZE`, `TEAMS`, and `SEED` taken from the matrix above:

```sh
"$BINARY" --generate-map gauntlet --width "$SIZE" --height "$SIZE" --teams "$TEAMS" --seed "$SEED" --json /tmp/gauntlet-evidence/profile-comparison/current.json --output /tmp/gauntlet-evidence/profile-comparison/current.map
```

The benchmark uses the same command with `SIZE=512`, `TEAMS=12`, and `SEED=1`, writing `bench.map` and optionally `bench.json`. It alternates before/after invocation order each repetition, records `time.perf_counter()` elapsed time, and takes the median of seven invocations per binary and output mode.
