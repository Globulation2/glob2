# Integration with the updated labour branch

The selected hospital policy (`29c9fdcc3`) and adopted surplus-worker towers
(`b806198f5`) were merged with upstream labour commit `8624e3b94` in `08b59bc63`.
Upstream adds reachable fruit, fitted force beliefs, army growth, gathered waves,
and its merge from master. The temporary diagnostic PR integration was not merged
into this production branch.

The runtime files merged without conflicts. Test conflicts were resolved by
retaining both sets of migration assertions, adding a combined version-109
hospital/fruit/force/wave migration check, and updating the reviewed literal
inventory and parameter count (664). A pre-existing structural assertion was
updated narrowly for upstream's wave/fruit policy reads, while continuing to
reject other strategy reads in those executors. No runtime behavior was changed
to accommodate that assertion.

The full Maxima runner passes all 19 suites: 17 native suites, the configuration
suite (14 tests), and the strategy-policy suite (12 tests). Broader Python
discovery passes 60 tests with two optional skips. Logs are retained here.
The configuration startup tests alone use `GLOB2_TEST_MAX_TICKS=512`.

Two engine runs exercise the merged code on Mac arm64 and Linux x86_64:

- The retained old version-109 initial save, 24,576 ticks. Hospital settings
  migrate to 0.6; newer fruit/force/wave policies retain their legacy settings.
- A fresh game on retained map `g15-s3001-nicowar/map-r0.map`, game seed 15001,
  players Maxima and Nicowar, 24,576 ticks. Hospital ratio is 0.6 and the new
  upstream policies are enabled.

**Both runs match across platforms at every tick**, including their detailed
entity records (see `comparison.json`).

Each run records every tick's aggregate checksum and a hash of its detailed
entity records. Commands, engine results and stream hashes are retained with the
records. The old input is `../compatibility/old-initial.game.gz`; the fresh map is
inside `../game-evidence.tar.xz`. Run commands from the corresponding built
checkout's root, replacing recorded machine-specific paths with local paths.

The 240-game ablation predates the upstream integration. These integration checks
do **not** re-estimate the combined branch's win rate. Windows remains untested.
The results in `comparison.json` describe the first integration stage above.


## Subsequent upstream save repair and integration fix

Merged upstream `be37a5d4c` (continuation state, format 113), followed by
`26b9788be` (documentation cleanup). Refreshing the golden fixture for the
confirmed hospital policy exposed one additional omitted field:
`Team::noMoreBuildingSitesCountdown`. At checkpoint 30,000 its value was 91;
the missing countdown explained the immediate team-only checksum difference.
This field controls acceptance of construction orders. Format 114 saves it and
loads it with a version gate and range validation. Earlier formats remain
readable, with their historical reconstruction behavior. The save floor remains
58 and replay floor 99; the network protocol is 39 to reject incompatible peers.

All 19 Maxima suites pass again. The unit continuation harness now exercises a
nonzero construction cooldown at five checkpoints, comparing 256 subsequent
ticks and RNG state each on Mac and Linux. Tournament/network, replay acceptance,
team save safety and entering-unit checks pass. Logs and `save-continuation.json`
retain the results.

With the selected 0.6 policy on Mac, saving at 24,576 and resuming through 32,768
matches all **8,192** uninterrupted team/entity records. A second scenario saves
at 30,000 and matches all **512** subsequent records. Its new version-114 fixture
passes on both Mac and Linux. The expected hashes come from uninterrupted
execution, not the resumed run. The version-113 fixture remains as a one-tick
legacy load check: its old 512-tick reference used the previous hospital policy,
so it is no longer a valid full-trajectory reference for current AI decisions.
The current 512-tick fixture replaces that trajectory check. Comparing the
uninterrupted format-113 and format-114 runs matches all 30,512 records, showing
that preserving the cooldown did not change this uninterrupted trajectory.

**Remaining portability failure:** loading the same version-114 hospital
checkpoint on Mac arm64 and Linux x86_64 matches the first 3,469 ticks, then
first differs at tick 28,045 in team 1 unit 1292's movement. All 4,723 remaining
ticks differ. Checked source files are identical; the first differing entity
is a diagnostic location, not a demonstrated cause. This longer check does not
pass, despite the shorter cross-platform and same-platform continuation checks
above. The branch needs this investigated before claiming cross-platform
simulation equivalence. Windows was not tested.

The `cooldown-*.tsv.gz` files retain both per-tick platform traces and source
stream hashes; `cooldown-platform-first-difference.json` retains the first
state difference. To reproduce, decompress `cooldown-checkpoint-24576-v114.game.gz`
and run the built game with absolute input/output paths:

```sh
build/src/glob2 --run-game --load-game /absolute/checkpoint.game \
  --ticks 32768 --telemetry checksums --output-dir /absolute/output
```

Commands and results for the uninterrupted and resumed Mac runs are retained
as compressed JSON. The hospital initial input is
`../compatibility/old-initial.game.gz`; the separate golden-fixture initial input
is `cooldown-fixture-initial-v112.game.gz`. The fixture checkpoint and expected
hashes are in `test/fixtures/save-continuation/`. The upstream removal of its old
investigation write-up and unused test fixture is preserved.
