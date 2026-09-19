# Constraint toolkit refactor evidence

**Historical numeric IDs:** these artifacts use Even Ground 59 and Marchland 60.
The later merge of The Gauntlet (master ID 59) moves them to 60 and 61 respectively.
Retain the recorded IDs when using the frozen binaries; use the textual generator
names with the current catalog.

This change reorganizes searches and strengthens shared contracts without retuning the
maps. Generator IDs, revisions, seed streams and golden rows remain unchanged.
The shared interface is described in [Constraint searches](../../../CONSTRAINT_SEARCH.md).

## Results

- **42 paired native requests matched**: 36 completed maps and six retained refusals.
  Saved map bytes, terrain output, exit/status/diagnostic, statistics, quality and all
  pre-existing telemetry matched between the frozen binaries.
- **464 golden checks passed**, including both experimental generators.
- **174 telemetry cases passed** with no semantic differences between collection off/on.
- Map-generator contract tests passed, including objective validation, state restoration,
  infeasible proposals, zero-move searches and disconnected ford candidates.
- Both Python test suites passed. Structured study, refusal and final-statistics smoke
  runs completed; malformed artifacts and process failures have separate classifications.

[requests.json](requests.json) lists the exact requests. The successful matrix covers
both generators, seeds 23/2001/41001, sizes 128×128, 256×256, 512×256, 64×512,
512×64 and 512×512, with 3–8 colonies. It includes default settings, zero search
budget, and wheat 0/wood 300. The remaining requests reproduce earlier random-study
refusals. [paired-results.json.gz](paired-results.json.gz) contains both runs' measurements,
map/terrain hashes, binary hashes and comparison results.
[provenance.json](provenance.json) identifies the source base and frozen binaries.
The baseline binary predates only documentation/evidence changes in that base.

The three intentional telemetry additions (`attempted`, `skipped`, and
`unjoined-components`) are excluded from the pair comparison; the baseline contains
none of those keys. No other telemetry is excluded. Busy-host timings are retained
for diagnosis, not presented as a performance benchmark.

## Reproduce

Build the base commit and this revision into separate frozen binaries, then use a
fresh output directory (native generation refuses existing result files):

```sh
python3 docs/map-generators/evidence/constraint-solved/refactor/compare.py \
  --before /path/to/base/glob2 --after /path/to/refactored/glob2 \
  --requests docs/map-generators/evidence/constraint-solved/refactor/requests.json \
  --out /tmp/constraint-refactor-comparison
scons -j3 release=1 server=0 build/src/glob2 map-generator-defaults-test map-generator-golden-test
build/src/MapGeneratorDefaultsTest /tmp/constraint-contracts
build/src/MapGeneratorGoldenTest /tmp/constraint-goldens
build/src/MapGeneratorGoldenTest /tmp/constraint-telemetry --telemetry
python3 test/test_map_generation_study.py
python3 test/test_map_fairness_tournament.py
```

The comparison script retains native JSON, generated maps, terrain reports and exact
commands in its output directory, writes compressed comparison results, and fails on
any difference. Checked-in logs record the local macOS ARM64 contract, golden,
telemetry, Python and smoke checks.

## Limits

This is behavior-preservation evidence, not a new fairness or gameplay claim. The
[prior review](../review/README.md) retains the gameplay experiments and known uneven
starts. No new human playtest or cross-platform per-tick checksum comparison was
performed. Floating-point map generation is not asserted identical across platforms;
the simulation implementation, save formats and network/replay gates are unchanged.
