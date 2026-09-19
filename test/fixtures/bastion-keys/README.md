# Bastion Keys review evidence

Final design: separate island estates, local farm piers only, mandatory swimming
between estates and to neutral keys. See the [design and preview](../../../docs/map-generators/BASTION_KEYS.md).
Earlier causeway experiments are excluded from this evidence.

## Provenance and reproduction

The generator is revision 1, final numeric ID **68**. Frozen swimming maps were
written while its development ID was 62; the large studies and profiling used
67. Upstream subsequently assigned those IDs to other generators. The retained
observations and metadata keep their original IDs. Reproduce current requests
with **68**, or resolve `bastion-keys` through the headless catalog. The golden
fingerprints remain identical on macOS ARM64 and Linux x86_64 after renumbering.
No simulation, AI, save-format, replay or network changes are part of this PR.

Build from the repository root:

```sh
scons -Q release=1 server=0 -j4 build/src/glob2 map-generator-defaults-test map-generator-golden-test map-generator-profile-fixture
build/src/MapGeneratorDefaultsTest /tmp/bastion-contracts --bastion-keys-only
build/src/MapGeneratorGoldenTest /tmp/bastion-goldens --require-rows
python3 .agents/skills/glob2-map-design/scripts/control_study.py bastion-keys ablation --out /tmp/bastion-controls --binary build/src/glob2 --jobs 6 --seeds 8
python3 .agents/skills/glob2-map-design/scripts/control_study.py bastion-keys random --out /tmp/bastion-controls --binary build/src/glob2 --jobs 6 --count 4000 --teams 1-12
```

Use fresh output/profile directories. Study metadata records exact historical
commands, control definitions and binary SHA256. `shape_sweep.py` records the
576-request size/count/worker envelope; change its output path for your machine.
`report.md` contains the control response tables. Raw compressed JSONL records
retain refusals and telemetry, not just successful cases.

| Study | Requests | Generated and validated | Explicit invalid requests |
| --- | ---: | ---: | ---: |
| One-control ablation | 705 | 654 | 51 |
| Random combinations | 4,000 | 1,719 | 2,281 |
| All 16 size pairs × 12 colony counts × 3 seeds | 576 | 135 | 441 |

Run `python3 test/fixtures/bastion-keys/verify_evidence.py` to check the retained
counts, hashes and complete game counters.

No generation failure or execution error occurred. All shape outcomes matched
the supported envelope. This samples the parameter space rather than proving
every possible combination. The independent reviewer also checked seed 947 at
512×512, 12 colonies, home 15, farms 18, five outlying keys per colony and maximum
crops: 72 distinct walking components (12 estates + 60 keys), all reachable by
swimmers.

All resource controls increase their corresponding tile counts over the sampled
steps. Starter wheat/timber and structural stone remain at zero abundance.
Farm size increases farmland and production; home size increases courtyard and
rampart size. Outlying-island placement saturates at available capacity (on the
sampled 256×256 maps, values four and five commonly place the same count).

## Playtests and compatibility

`calibration-swim.json` contains the final measurements for seed 101, four
colonies, game seed 2, 20,000 ticks (Cortex ended early). Nicowar had 31–51 worker
births per colony and Maxima 11–12, both with zero worker starvation. Cabino had
nine worker starvation deaths per colony and no timber harvest; Cortex resigned
early. These AI limitations also occurred on the matched Forts reference; this
is not a claim of universal AI support or human-tested competitive balance.

`swim_games.py` records the long-game commands: seeds 211 and 307, all four team
rotations for Nicowar, rotation zero for Maxima, game seed 2, 60,000-tick cap.
`games/` contains native results, complete measurement timelines and a compact
summary. `collect_games.py` checks four `final=1` records whose ticks exactly
match each native result. Fruit harvesting proves neutral-island visits; melee
damage to enemy buildings proves attacks across water under the validated
walking-isolation contract. First-event ticks are telemetry sampling bounds.
Conversions are recorded separately from births and combat/starvation deaths.

All ten games completed 60,000 ticks with valid final counters. Across the eight
Nicowar games, all 32 colonies damaged enemy buildings and 31 harvested neutral
fruit; 12 colonies were eliminated. Per-colony births ranged 62–430 and worker
starvation deaths 0–29. Across two Maxima games, all eight colonies attacked enemy
buildings and five harvested fruit; all survived, with 66–392 births and 6–93
worker starvation deaths. The larger late-game starvation counts are a limitation,
not evidence of a starvation-free economy. Outcomes include combat and conversion;
the observed population spread is not a direct measure of starting-site fairness.

`211-r0.map.gz` and `211-r0-nicowar-final.game.gz` retain a playable initial map
and resulting saved game. Decompress outside the repository before loading.
`compatibility.replay.gz`, `macos.checksums.gz` and `linux.checksums.gz` record a
separate 5,000-tick Nicowar game on that same initial map, game seed 917.
The decompressed checksum sidecars are byte-identical, SHA256:
`b3221954eb854209b0a8c6602ffd9b38fa4c0344cd9d2b74df60750a3184f364`.
Windows execution was not checked locally; CI builds do not replace that check.

## Profiling and review

`profile-before.sample.txt` identified full-map scans for every short gate and
pier path. `localLane` now bounds those scans to each path's short unwrapped
extent while retaining shared stroke rasterization and four-corner wall removal.
`profile-after.sample.txt` records the resulting profile. Shared fertility and
flood analysis remain substantial costs; no shared engine behavior was changed.

`profile-timings.json` records sequential paired fixture runs, 60 rounds per
version and seed, measured with child-process user + system CPU time. Command:
`MapGeneratorProfileFixture PROFILE SEED 60 bastion-keys`.

| Seed | Before CPU seconds | After CPU seconds | Reduction |
| --- | ---: | ---: | ---: |
| 917 | 3.559 | 2.501 | 29.7% |
| 918 | 2.847 | 2.131 | 25.2% |

The machine was shared with other builds, so wall-time comparisons are noisy.
`profile-world-comparison.jsonl` records 96 byte-identical native maps before and
after optimization across seeds, sizes, counts and extreme controls.
`compare_profile_worlds.py` documents that comparison; it requires the frozen
pre-optimization binary under `artifacts/bastion-keys/profile-before/` (source
commit 7819e1c54) and the optimized binary under `profile-after/` (source commit
bdb7952cb). Both frozen binaries use ID 67. Do not substitute the current binary
there: ID 67 now means Drowned Forest, and native map names themselves include
the numeric ID. Current ID 68 is instead checked through the matching golden
fingerprints, which exclude that study-name metadata.

Repeated map-review rounds covered appearance, starter crops, reachable service
courts, swimming isolation and the optimization. Final merged-source review found
no code blocker; its reproduction-ID correction was applied. Translation review
and limitations are recorded in `translation-review.md`. Native-speaker idiom
and every translated widget's fit were not independently confirmed.
