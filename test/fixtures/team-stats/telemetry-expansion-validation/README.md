# Gameplay telemetry expansion: validation evidence

The comparison executable was built at parent commit
`c8a24ba3e01a4005604247cb19ba168504055d60` with the same macOS SCons
`-O3` settings as the change. Its SHA-256 was
`167be8eef900cc8f031ff11f5f4b81c9abc200132de4852bd473cddee2b59c4b`;
the final changed executable was
`f8c8e678c980010231900e20e8466f3c03b4d52f0ca4ff5a1366ad4e28940c05`.
Saved games contain the RNG state and orders. The four-AI input
`games/gd-large-4ai.game` has game seed 2 and SHA-256
`792110aa5f100d2b23187bb819f847c4e0c9ac172c29aebb50620ec2ce5ee530`;
the 12-team `games/gd-bigarena-long.game` has game seed 4 and SHA-256
`11dd4fcadf0ff8cd2341408e665ce4207e69849c14d105f780762baeed3fffdc`.

## Simulation and save continuity

Run `python3 test/check_telemetry_simulation.py build/src/glob2` to reproduce
the [checksum results](checksums.log). The command compares every byte of
the 1,024-tick four-AI sidecar (SHA-256
`3f36e16da805f072bb5bc52481dcde29043814e1cd15457b9a3400f2d9faf1c5`)
and 2,048-tick 12-team sidecar (SHA-256
`9890c690cd768897e79458f8bd70c590c3b5b9d6a184434069a8751889fbdf86`)
against compressed sidecars captured from the parent commit. The parent
fixtures are
[`four-AI`](../telemetry-expansion-gd-large-4ai-1024.checksums.gz) and
[`12-team`](../telemetry-expansion-gd-bigarena-2048.checksums.gz).

The same command loads the attached
[`format-108 checkpoint`](checkpoint-1024-v108.game.gz), simulates from tick
1,024 to 1,279, and compares each team/entity checksum record with the
[`parent checkpoint reload`](../telemetry-expansion-gd-large-4ai-checkpoint-parent-reload-256.checksums.gz).
All 256 records match. The checkpoint's uncompressed SHA-256 is
`a07502e108548d24009c7f058545b04f7cd65356b27f4f17920c835bf4c72f5d`.

**Limit:** On this four-AI save, both parent and changed builds have an
existing mid-run save/load divergence from uninterrupted execution: entity
records first differ at tick 1,031 after a tick-1,024 checkpoint. The parent
and changed checkpoint continuations match each other in all 256 team/entity
records. Aggregate checksums from the two checkpoint files differ because
`MapHeader::checkSum()` includes the save-format minor version (107 versus
108). The two direct, uninterrupted runs above have byte-identical aggregate
and detailed sidecars. This change did not fix the pre-existing checkpoint
divergence; save/load scenarios in the statistics harness also pass.

## CPU and memory

The benchmark uses `test/benchmark_telemetry_expansion.py`, six mirrored
ABBA/BAAB blocks per save (24 runs, 12 of each binary), process **user CPU**
seconds, and the median of each version. Wall times and every run are retained
in the raw JSON. No checksum or export output is enabled during the CPU runs.
The parent binary was copied to `/tmp/glob2-telemetry-baseline` before edits.

| Saved game | Ticks/run | Parent median CPU | Changed median CPU | Change | Raw runs |
| --- | ---: | ---: | ---: | ---: | --- |
| Four AI, seed 2 | 8,192 | 2.963 s | 2.948 s | −0.48% | [cpu-4-ai.json](cpu-4-ai.json) |
| 12 teams, seed 4 | 2,048 | 4.902 s | 4.891 s | −0.24% | [cpu-12-team.json](cpu-12-team.json) |

Commands, including exact binary/save paths and hashes, are also in each JSON:

```sh
python3 test/benchmark_telemetry_expansion.py /tmp/glob2-telemetry-baseline build/src/glob2 games/gd-large-4ai.game --ticks 8192 --pairs 6 --order abba --output cpu-4-ai.json
python3 test/benchmark_telemetry_expansion.py /tmp/glob2-telemetry-baseline build/src/glob2 games/gd-bigarena-long.game --ticks 2048 --pairs 6 --order abba --output cpu-12-team.json
```

Peak resident memory on a single 12-team run rose from 66,256,896 bytes to
74,432,512 bytes (+7.80 MiB), measured with `/usr/bin/time -l` and the same
2,048-tick save/command. Logs: [parent](glob2-telemetry-rss-before.log),
[changed](glob2-telemetry-rss-after.log). The extra memory holds per-team
building-coverage overlap counts and measurement history; process CPU stayed
within the one-percent target. Timing was measured locally on macOS; CI
checks deterministic execution and compatibility on Linux and Windows.

## Functional and UI checks

The [statistics harness](team-stats-harness.log) passed current-format
save/load, overlapping building footprints, health thresholds, distance
boundaries, mid-interval growth continuation, AI telemetry, numeric corruption
controls, and disposable-profile checks. Older saves passed the
[version-88](glob2-telemetry-packed-legacy88.log) and
[version-84](glob2-telemetry-packed-legacy84.log) fixtures. The
[save safety](glob2-telemetry-packed-safety.log) and
[distributed telemetry](glob2-telemetry-packed-distributed.log) checks passed.
All 16 software-rendered 640×480/1024×768 screenshot pages were generated;
selected [live measurement](screenshots/live-640x480.png),
[expanded measurement](screenshots/live-expanded-640x480.png),
[growth graph](screenshots/graphs-4-640x480.png), and
[threshold graph](screenshots/graphs-5-640x480.png) views are attached.
The [translation](glob2-telemetry-translation-test.log),
[catalog audit](glob2-telemetry-translation-audit.log), and
[font coverage](glob2-telemetry-font-test.log) checks passed after replacing
English placeholders in non-English catalogs with compact labels assembled
from each catalog's translated base terms.

`scons -j8` and `git diff --check` passed. Manual gameplay and platform CPU
profiling were not part of this local validation; Linux and Windows CI results
must be checked before merge.
