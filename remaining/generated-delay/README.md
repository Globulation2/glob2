# Tick-delay ablation on real maps against current master

Branch test source `3794e7b116ef3ade7b6e293d2f0554a6e589b9ec`, shipping growth implementation `b1e8f5a3c`, master comparator **`aee20ae527f9648d2f41791a2732f90605078439`**, fetched at the start of this campaign. This is a full current-master executable comparison, not the retained immediate-growth pass and not the previous PR version. Master uses its own unmodified production `Game::syncStep` and resource-growth implementation; only the test driver is added. The source audit checks2187 source/build/test files against the pinned tree, with only the test addition differing.

## Inputs and execution

Twelve actual master generators, default controls, two colonies,20 seeds each:

- River/Swamp128².
- Crater Lakes, Islands, Rain Shadow, Old Growth, Braided River, Stone Highlands and Tidal Flats256².
- Canals256×128, Fjord Continent512×256 and Continents512².

All240 worlds generated successfully; no seed substitution. Master ran each for4096 full simulation ticks. The branch ran the same worlds with delays1,2,3,4,8,12,16 in owner1 and shared4 modes:3360 branch runs plus240 reference runs. Every tick checks owner/shared checksums and independent material-stock/deposit accounting against team growth statistics. These are no-player/no-harvesting/no-building runs: the actual generator terrain/resources remain, but starting colony entities are removed. Original worlds with colonies and stripped/endpoint inspection saves are attached for seed1. These saved worlds have no player seats; they are not advertised as ready-to-play matches.

Master and branch save formats differ, so branch inputs come from exported master fixtures, not independent same-seed regeneration. The importer verifies canonical resource catalogs, each deposit's type/variety/material stocks and every cell's growth permission, habitat and ecology rates for ALL renewable resource types. Classic vertices are transferred exactly. Those static growth inputs match on every world; branch arms then load identical native saves. Master and imported branch RNGs start from the same explicit seed. Private batch RNG streams and delayed original-snapshot decisions remain intentional differences between engines. Initial game checksums across different engine versions are not expected to match; within each delay, owner/shared checksums do match.

Master fixture exports are under `master/<generator>/fixture-<seed>.json.gz` in the evidence tree. Full baseline results/logs with corrected embedded source provenance are under `master-verified/`. Their fixtures and result JSON are byte-identical to the initial export run; see `master-rerun-identity.json`. The original archive build inherited its enclosing repository's revision in generated test metadata, despite matching master production sources; its logs remain for audit. A private Git index pinned the copied source to master, then the entire reference campaign was rerun. Acceptance uses the corrected baseline binary/provenance, not the erroneous metadata label.

Continents was split into seed shards after its original run was deliberately interrupted. Seed1 comes from the original run; seeds2–20 come from four passing shards. All overlapping ecology endpoints, checkpoints, statistics and pipeline counts match exactly. Initial whole-game checksums differ between independently imported processes, so cross-process full-state identity is not claimed; owner/shared per-tick identity passes within every trial. See `continents-shard-identity.json`. The original interrupted JUnit is retained and is not an accepted complete test. The later binary adds only test seed-range selection; both source/binary hashes are recorded.

## Results

[Overview chart](charts/delay-overview.png) · [Map-family chart](charts/map-family-effects.png). SVG and PDF versions are also supplied.

# Delay ablation on current-master generated worlds

| Delay | Wheat vs delay8 | Wood vs delay8 | Algae vs delay8 |
|---:|---:|---:|---:|
| 1 | +0.073% [+0.021, +0.125] | +0.222% [+0.156, +0.289] | +0.242% [+0.129, +0.346] |
| 2 | +0.058% [+0.013, +0.109] | +0.174% [+0.113, +0.236] | +0.241% [+0.135, +0.343] |
| 3 | +0.065% [+0.018, +0.114] | +0.130% [+0.062, +0.197] | +0.210% [+0.113, +0.300] |
| 4 | +0.035% [-0.015, +0.088] | +0.104% [+0.040, +0.164] | +0.182% [+0.104, +0.258] |
| 8 | +0.000% [+0.000, +0.000] | +0.000% [+0.000, +0.000] | +0.000% [+0.000, +0.000] |
| 12 | -0.044% [-0.082, +0.000] | -0.147% [-0.205, -0.093] | -0.081% [-0.181, +0.025] |
| 16 | -0.066% [-0.109, -0.019] | -0.269% [-0.341, -0.202] | -0.145% [-0.251, -0.033] |

Equal weight per generator;100000 paired-bootstrap95% intervals. Final stocks,4096 ticks.

| Delay | Wheat vs master | Wood vs master | Algae vs master |
|---:|---:|---:|---:|
| 1 | -0.064% [-0.176, +0.054] | +0.000% [-0.210, +0.233] | +0.024% [-0.487, +0.401] |
| 2 | -0.078% [-0.183, +0.034] | -0.047% [-0.260, +0.187] | +0.023% [-0.493, +0.419] |
| 3 | -0.072% [-0.177, +0.041] | -0.091% [-0.303, +0.139] | -0.007% [-0.534, +0.386] |
| 4 | -0.101% [-0.212, +0.016] | -0.118% [-0.324, +0.110] | -0.036% [-0.555, +0.354] |
| 8 | -0.136% [-0.249, -0.020] | -0.221% [-0.429, +0.014] | -0.215% [-0.712, +0.160] |
| 12 | -0.180% [-0.294, -0.060] | -0.368% [-0.563, -0.143] | -0.295% [-0.771, +0.085] |
| 16 | -0.202% [-0.327, -0.078] | -0.489% [-0.689, -0.269] | -0.358% [-0.806, -0.003] |


These aggregate percentages give each fixed generator equal weight. `analysis.json` retains every generator/resource/delay comparison, absolute added stock, initial/final stocks, deposit counts, growth-only changes, per-generator intervals and normal-versus-flushed endpoints. Each raw case includes128-tick checkpoints, so512 and4096 results come from one trajectory, not independent samples. A terminal flush publishes pending outputs without computing more growth; it is diagnostic and never counted as ordinary gameplay.

## Statistical interpretation

100000 paired seed-bootstrap draws. Seed IDs are resampled jointly across generators to retain possible correlation from shared random streams. Generators themselves are fixed; uncertainty covers these seed outcomes, not all conceivable maps. Tabulated final-stock and growth-only intervals are percentile95% and are not multiplicity adjusted. The overview chart instead uses approximate simultaneous95% bands from the bootstrap maximum absolute standardized deviation across materials/delays within each comparator row. Map-family chart intervals remain pointwise and are labeled accordingly. Exact directional paired sign tests receive Holm correction over the generator/material/delay comparisons within each horizon/endpoint/comparator family.4096 normal endpoints versus delay8 are primary;512 checkpoints and flushes are secondary/diagnostic. Owner/shared copies are never independent replicates. Any zero-denominator aggregate exclusions are explicitly listed. Report small absolute growth denominators alongside percentages.

Delay-versus-delay isolates schedule changes. Delay-versus-master also includes snapshot feedback/randomness and the rest of the PR; it must not be presented as entirely caused by delay. This is ecology/accounting evidence only: heavy checksums synchronize workers, and concurrent test processes are used to finish independent cases. No throughput, CPU or reserved-core speedup claim follows from these elapsed times. No AI/harvesting balance, cross-platform determinism or threadless validation is established.

## Reproduction

Executable/source/dependency and all240 fixture hashes are in `freeze.json`; build commands/flags and failed compile attempts are retained. `master-source.patch` is the test-only addition against the pinned master tree. Build both engine test binaries with the same release dependencies as the logged SCons commands. The branch harness is committed; the temporary master driver and experiment scripts are published here without adding experiments to shipping CLI controls.

The campaign runner records the exact command, working directory and relevant environment in each case's `command.json`. From the branch root:

```
python3 docs/.work/run-generated-delay-master.py master
python3 docs/.work/run-generated-delay-master.py branch
OPENBLAS_NUM_THREADS=1 /home/bradley/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 docs/.work/analyze-generated-delay.py
```

The runner uses `LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib`. For an individual branch case, set `GLOB2_GROWTH_GENERATED_WIDE=1`, `GLOB2_GROWTH_GENERATED_CASE=river`, `GLOB2_GROWTH_GENERATED_INPUT=<master-fixture-root>`, `GLOB2_GROWTH_GENERATED_OUTPUT=<output.json>`, `GLOB2_GROWTH_GENERATED_TICKS=4096`, and `GLOB2_GROWTH_GENERATED_DELAYS=1,2,3,4,8,12,16`, then run `python3 test/run_tests.py --binary engine --no-display -j1 --timeout 7200 --filter 'ResourceGrowthBenchmark/generated*'`. Master uses the published added test with `GLOB2_GROWTH_MASTER_OUTPUT` and the same case/tick settings. Analysis needs NumPy; bundled Python paths are environment-specific, not mandatory.

Linux x86-64, GCC15.2, release/O3,server=0,optimized_assets=0. The18 existing growth/accounting cases and simulation-version gate also pass. Independent review tightened fixture equality, renewable coverage, missing-input checks, aggregate seed resampling and exclusion reporting. The PR remains draft: this test uses matching classic growth inputs across engine versions; it does not integrate master's vertex-terrain/save-format changes into the branch or establish performance of the current PR head.
