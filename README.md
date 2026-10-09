# Building area effects: review evidence

Tested PR head: `0c47097e83edf0ae036f4339e0043f9c4a71f2c2`.
Integration base: `f650014effcbdb4e1c7eabcaab02c88a860c1faa` (fetched master).
The feature branch contains one commit on that base. Independent review fixes
are described in [review-team.md](review-team.md).

## Environment

Linux x86-64, GCC 15.2.0, release `-O3`; deterministic simulation uses
`-fno-fast-math -ffp-contract=off`. SDL 3.4.16, SDL_image 3.4.6,
SDL_ttf 3.2.2 and SDL_net 3.2.0 use the local SDL prefix.
No runtime dependencies changed. The supplied headless SCons hook skips artwork
export; display and the complete asset pipeline were not verified.

## Final verification

`pr-engine.xml` and its log contain the final selected inventory: 209 passed,
0 failures and 2 display skips (211 cases). Coverage includes all 16 area-effect
fixtures, catalog identity and bounds, independent coverage oracle, combat,
services, immediate and asynchronous growth, immutable snapshot jobs, save/load
continuation, historical save safety, replay/network acceptance and multi-client
per-tick agreement. `continuation-evidence.zip` retains generated saves, replays,
match records and checksum traces from these harnesses.

`pr-unit.xml`: 899 passed, 2 failing cases (3 assertions), 20 display skips
(921 cases). The failures are unchanged graphics code: 16-bit PNG rounding
with this local SDL SDK and sprite loading without exported resource assets.
This run exited 1 and is not claimed green. Strict translation validation also
retains 448 existing generator-studio English fallbacks, matching the original
checkout; structural validation passed.

Build exited 0. Simulation revision/golden-record validation exited 0; 31
changed-path selector tests passed. Both affected browser test scripts passed
syntax checks; browser execution was unavailable. The browser fixture was
regenerated natively at save format 150, seed 42, 1500 ticks.

Windows, macOS, Android, browser runtime and secondary-compiler checksum
comparisons were unavailable. Linux continuation/multi-client agreement does
not establish cross-platform determinism. No expensive hosted CI was requested.
Maintainer acceptance is not yet recorded; the PR remains draft.

## Performance

The reviewed benchmark source is `8d8b9e40f6af1651a735d67bf096f57e916ae050`;
final head differs only in the building-catalog guide. Runtime, tests, compiler,
flags and dependencies are identical. The script and JSON retain exact commands,
execution order, wall times and load averages. Five alternating runs, five
repeats each, were pinned to CPU 24 on a shared host. This fixture uses 256
healthy workers on a 256-square map and wall emitters with free upkeep. It does
not benchmark combat, enemy damage or active resource growth.

| Emitters | Disabled steady ns | Enabled steady ns | Difference | Disabled pulse ns | Enabled pulse ns |
|---:|---:|---:|---:|---:|---:|
| 0 | 139790 | 130619 | -6.56% | 154769 | 140370 |
| 32 | 150559 | 140809 | -6.48% | 167819 | 173139 |
| 128 | 159169 | 160699 | +0.96% | 173249 | 208559 |
| 512 | 241168 | 246248 | +2.11% | 277189 | 376047 |

Negative rows illustrate host noise and are not evidence of speedups. The
user waived the 1% disabled threshold. Earlier original-engine comparisons are
retained in matched-summary.csv and matched-runs.json; the reviewed workload
is a fixture measurement, not a general performance guarantee.

The separate dense-field matrix covers 256/512/1024 square maps, 1/4/16 teams,
and 0/32/128/512 emitters with all channels, paid upkeep, overlap, diplomacy
changes and mass removal. All 12,205 assertions passed. Stationary maintenance
visits no emitters and allocates no new field/scratch buffers, approximately
0.1 microseconds across emitter counts. Counters do not measure every heap
allocation. At 1024-square/16 teams, the dense payload is 226 MiB, cumulative
fixture RSS peaks around 381 MiB; bookkeeping and growth snapshots add memory.
At 128 emitters, initial allocation/rebuild was 145 ms, funding 25 microseconds,
diplomacy 6.46 ms, mass removal 4.73 ms. At 512: initial 156 ms, funding
106 microseconds, diplomacy 16.47 ms, removal 9.08 ms. A hypothetical 21-team
payload is 296 MiB; the current team limit is 16.

## Commands

See [commands.txt](commands.txt), raw logs, JUnit reports and benchmark script.
