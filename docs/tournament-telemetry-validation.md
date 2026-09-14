# Tournament integration validation

Validated on macOS arm64 on 2026-09-14 after rebasing the three telemetry commits
onto `1537cb280` (master). Integration adds streaming offline readers and fixes
structured final-save performance coverage, instant-construction consumption and
training visits under disabled upgrades. Gameplay/AI save fields now use versions
105/106, keeping master's 101–104 field layouts, minimum 58, replay floor 99 and
network/YOG gates 33.

| Check | Result |
| --- | --- |
| Optimized client + team/save/performance harness builds | Passed |
| Team statistics harness | Passed: event scenarios, new rule interactions, 32 sampling phases, binary/text persistence, malformed fields, AI schemas, shared-team identities, controller generations, replay boundary 98–107 |
| Save safety harness | Passed, including unchanged disposable preferences |
| Genuine versions 84 and 88 | Load successfully; statistics match retained expected output |
| Existing structured CLI suite | 14 cases passed, including configuration/save continuation and map reports |
| Tournament protocol tests | 20 passed |
| Existing distributed map readers | 2 passed |
| New game telemetry readers | 2 passed: exact uint64/int64, escaped strings, dynamic fields, missing/truncated data, compressed artifact verification, typed JSONL/CSV |
| Real worker/coordinator integration | 17 accepted jobs: one map, four AI pairs, four export-off comparisons and eight reloads |
| Export on/off | Identical per-tick checksums for each of the four pairs |
| Repeated reload at tick 513 | Identical checksums and gameplay/AI records through tick 700, all eight AIs |
| Retained exports | All gameplay/AI/performance families and final records present; offline counts equal transferred log counts |
| Final saves | Requested final saves appear before the session's performance final records |
| Master comparison | All eight AIs: 14,332 matching per-tick records plus identical team outcomes, from initial and tick-513 master-format-104 saves through tick 2048 |
| Old coverage | New measurements from master-format-104 continuations start at tick 513 |

No throughput benchmark was rerun: the previously accepted timing evidence and its
limits remain in [performance validation](performance-telemetry-validation.md).
Runtime verification here covers localhost macOS arm64 only. SSH transport was
not changed; new telemetry has not been run on remote Linux/Windows hosts in this
integration. No new interactive UI changes or playtesting are claimed. Repeated
reload tests do not assert that historic Numbi/Castor unsaved decision state is
identical to uninterrupted play.

## Reproduction and evidence

The retained archive and file/hash index are
[test/fixtures/tournaments/game-telemetry-20260914.tar.gz](../test/fixtures/tournaments/game-telemetry-20260914.tar.gz)
and [its index](../test/fixtures/tournaments/game-telemetry-20260914.index.json).
They contain accepted records and verified artifact objects, worker package and
bundle identity, master comparison inputs/traces/results, source patch, scripts
and test logs. Large executable/data bundles remain in
`output/tournament-telemetry/bundles/`; master was built in the isolated checkout
`/tmp/glob2-telemetry-master`. The archive is sufficient for offline reanalysis:

```sh
mkdir /tmp/game-telemetry-evidence
tar -xzf test/fixtures/tournaments/game-telemetry-20260914.tar.gz -C /tmp/game-telemetry-evidence
python3 -m tools.tournaments.ai_comparison reanalyze /tmp/game-telemetry-evidence/distributed/localhost --draws 0
```

Build and test commands (full outputs retained):

```sh
scons -j8 release=1 server=0 build/src/glob2 team-stats-save-test savegame-safety-test performance-telemetry-test
python3 test/run-savegame-safety-tests.py --check-preferences build/src/TeamStatsSaveHarness .
python3 test/run-savegame-safety-tests.py --check-preferences build/src/SavegameSafetyHarness .
build/libgag/src/PerformanceTelemetryHarness
python3 test/tournament_cli_integration.py --output output/tournament-telemetry/cli
python3 test/test_tournaments.py
python3 test/test_distributed_map_telemetry.py
python3 test/test_distributed_game_telemetry.py
python3 test/distributed_game_telemetry_integration.py --hosts output/tournament-telemetry/hosts.json --output output/tournament-telemetry/distributed
python3 output/tournament-telemetry/compare-master.py
```

The master comparison script retains every executable argument, uses generator 15,
128×128, map seed 42 and game seed 19, with pairs Numbi/Castor, Warrush/Econo,
Nicowar/Cortex and Maxima/Cabino. Its initial and tick-513 saves are written by
unmodified master; both binaries then consume those exact inputs. Raw checksum
traces compare every recorded component, not just replay orders.

The first statistics-harness run exposed an outdated test fixture: cycling AI
implementations on one player reused Cortex tuning as Maxima tuning. The fixture
now clears implementation-specific settings between synthetic controllers. The
corrected harness passes; no production AI behavior was changed to accommodate it.
