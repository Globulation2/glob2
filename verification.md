# Parallel asset packaging verification

Tested commit: `7a5f721abb00d5653e31bd09f37e56b98acff8af`. Base: `9a08b91f4350649862e788135e99bb1f5f469e3a`.

Host: Linux-7.0.0-31-generic-x86_64-with-glibc2.43. Pinned encoder: 3.14.4 (main, Aug 20 2026, 10:41:58) [GCC 15.2.0]
Pillow 12.2.0 libwebp 1.6.0. Validation was confined to CPUs 12–15 and 28–31 (four physical cores with SMT siblings).

## Results

- Focused asset suite: 34 tests passed, including serial/parallel cold-cache byte/audit equality for both encoding policies, sprite sheets, duplicate inputs, subprocess budget handoff, and worker-failure rollback.
- Full build-system suite: 545 tests passed in 86.068 seconds; three skips are recorded in the log (two unavailable pinned SDL source archive checks and fontTools).
- Real SCons integration: `scons -f artifacts/parallel-assets/SConstruct -j4` completed a four-image export through the pinned interpreter and spawned workers.
- Timing sample: 16 repository artwork PNGs, two runs each at one and four workers, independent cold caches. Output SHA-256 hashes matched across all runs. Individual timing pairs are shown below. The host was busy with other work, so absolute times varied; this is a sample demonstration, not a complete-release-build speed claim.

| Round | One worker (s) | Four workers (s) | Ratio |
| --- | ---: | ---: | ---: |
| 1 | 133.13 | 41.02 | 3.25× |
| 2 | 57.62 | 17.07 | 3.38× |

## Commands and evidence

`environment.json` records exact commands, revision IDs and dependency versions. `package-tests.log`, `build-system-tests.log` and `scons-integration.log` contain complete logs. `benchmark.py` and `SConstruct` reproduce the sample and SCons smoke export when copied under `artifacts/parallel-assets/` in a checkout of the tested revision. `benchmark-results.json` lists source assets, timing observations and output hashes.

## Coverage and omissions

Linux x86-64 execution plus cross-platform build contracts. Native Windows/macOS builds and a full production release package were not executed. Encoding policies, exported bytes and ordered audits are covered by focused comparisons. No simulation, save, replay, network, runtime dependency or gameplay code changed; those engine tests are outside this tooling-only change. Worker processes consume memory in addition to concurrent compiler jobs, so the build budget may need to be lowered on smaller machines.
