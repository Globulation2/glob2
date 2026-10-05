# Bounded Cortex feeding investigation

No terrain-property classification or inn-placement migration defect was found. The sustained last-treeline-1427/seed101 failure is a stocked but capacity-limited inn with no site accepted by Cortex's existing placement rules. Those rules and the spare-inn requirement for upgrades are unchanged by this refactor. The observed starvation regression remains real; identifying its mechanism does not establish balance equivalence.

## Evidence

The original frozen baseline and candidate were rerun from the same map/seed/AI seats to tick32768, adding replay recording and saves every8192 ticks. All128 periodic measurement snapshots before the final stopping boundary match the respective original game exactly. The two final32768 snapshots differ in a few counters because one is a stopping-boundary final sample and the original is a continuing-game history sample; this was not used as a state-equivalence assertion.

The structured CLI intentionally removes Cortex diagnostic environment variables. Each retained checkpoint was therefore loaded through the legacy `-nox CHECKPOINT STOP_TICK 1` entry point for256 ticks with the existing decision/inn/worker CSV diagnostics. At candidate tick16385 the inn has6/10 wheat and4/4 eaters, against73 units, feedCapacity28 and18 starving units. At32792 it has9/10 wheat and4/4 eaters, against67 units and14 starving. This rules out empty storage as the immediate bottleneck. The feed-capacity action is ineligible despite a capacity deficit and no inn construction site.

An isolated diagnostic object, linked with existing engine/test objects, loaded eight checkpoints (baseline/candidate at8192,16384,24576,32768). It evaluates the production placement helpers and counts each gate, then checks its survivor count against production `Cortex::placeCandidates`. All24 assertions passed. Production source and production binaries were untouched.

Across all eight snapshots:

- Every cell agrees between the legacy `terrain sprite <16` building predicate and the new `buildable` property: zero mismatches.
- Canonical terrain counts are identical, with no road or ice cells. Ground movement modifiers and air constraints are disabled.
- The current engine's resource/building footprint rules and Cortex candidate scan produce zero further inn sites in both trajectories at these sampled times. Baseline had already secured its second inn earlier.

Candidate tick32768 gate counts:14902 discovered corners;8812 resource-free, building-free, buildable grown footprints;93 near wheat;6 with the required surviving-wheat cluster;zero with surviving wheat immediately adjacent to the grown footprint. Independently,29 locations satisfy every gate except resource occupancy. Thus nearby potential building space is occupied by resources, while existing empty space fails the unchanged wheat-adjacency requirement.

At candidate8192, two empty grown footprints satisfy the wheat requirements but both fail the unchanged6-tile inn-spacing rule; one also violates inn-side clearance. Source `scoreInnUpgrade` requires a redundant inn and feeding slack, or a valid spare-inn site, before upgrading. The missing second site therefore also prevents the first inn's feeding capacity from increasing.

## Source review and limits

`src/ai/cortex/CortexPolicyEconomy.cpp::scoreFeedCapacity` declines without a valid placement candidate. `CortexPlacementCandidates.cpp` applies grown-footprint space, wheat cluster/adjacency, spacing and clearance requirements. `CortexPolicyTech.cpp::scoreInnUpgrade` enforces spare capacity. None of these files changed in this refactor. Cortex's only direct terrain edits are two equivalent classic-water-to-swimmable queries in `CortexWater.cpp`. Shared `Map::checkTile` changes building terrain permission to the property lookup; cell-by-cell comparison above verifies equality on the actual affected saves.

The diagnostic establishes why the sampled state cannot expand feeding capacity. It does not identify the first divergent random draw or prove which earlier growth/placement choice caused the trajectories to separate. It does not prove all hungry units have an open route to the inn; full slots already provide a concrete admission bottleneck. Clearing resources, relaxing spacing, changing upgrade policy or adjusting birth allocation would be broader AI behavior changes, not correction of a demonstrated terrain-property defect. No such changes or ecology coefficient tuning were made.

## Reproduction and artifacts

All paths below are within `artifacts/terrain-refactor/ecology/full-games/`:

- `trace_cortex.py`: exact frozen-binary rerun commands; original commands and replay/checkpoint outputs are retained under `cortex-investigation/{baseline,candidate}`.
- `probe_cortex.py`: bounded legacy CLI diagnostic invocations and environment settings; CSVs and logs are under each variant's `probe-{8192,16384,24576,32768}` directory.
- `cortex-investigation/PlacementAudit.cpp`: complete isolated diagnostic source.
- `cortex-investigation/build_diagnostic.py` and `build-commands.json`: exact compile/link construction from existing build objects.
- `cortex-investigation/placement-audit.log`: gate counts, building positions, classic-property comparisons and assertion results.

Diagnostic execution:

```sh
taskset -c 0-11 artifacts/terrain-refactor/ecology/full-games/cortex-investigation/placement-audit --test-suite=TerrainDiagnosis
```

No further runs are pending.
