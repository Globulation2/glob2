# Natural growth on real generated maps

Tested source `dd7391a1eaa9a0877af73e5eaa1aabf9a567bad1`, simulation implementation `b1e8f5a3c`. This follow-up changes test code/documentation only. Integrated base `67fd5b935`; fetched master `285dd5b8f499f0925ddc42e0c2b34667a53704b8` is not integrated. These are the branch's real lobby generators, not painted approximations and not an executable comparison against latest master.

River and Swamp128², Crater Lakes and Islands256², default generator controls, two starting colonies, seeds1–20. No generation failures or substituted seeds. Original generated maps retain all terrain and resource placement, including fruit/stone where generated; analysis below isolates wheat, wood and algae. Starting units/buildings are removed through the engine's uncontrolled-team cleanup for the no-harvesting comparison. Every variant loads the exact same serialized world, and initial checksums must match. Representative seed1 original worlds with colonies, stripped inputs and endpoints are included as `.game` files. These have no player seats and are inspection artifacts, not ready-to-play matches. Generator revisions/options are recorded per sample.

Each of the80 worlds runs immediate-reference, delayed owner1 and shared4, with delay8. There are240 runs per horizon,480 across512 and4096 ticks. The two horizons use the same seeds and are not independent statistical replicates. Every tick compares owner/shared heavy checksums and reconciles material stocks/deposit counts against global growth statistics. All runs passed. The immediate reference calls the retained pre-snapshot `Map::growResources()`; the delayed arms run full `Game::syncStep`. Private RNG streams and lack of within-batch mutation feedback remain expected differences. These experiments measure unharvested ecology, not sustainable economy or throughput. Heavy checksums join workers, so timings are not performance evidence.

## Results

Percentages below compare mean final material stocks. Existing starting stocks can dilute growth differences; the last column instead compares newly added stocks. Analysis JSON includes initial stocks, absolute changes, deposit counts, growth-only intervals and terminal-flush counts. The flush publishes pending work without more growth calculation and is a diagnostic only.

### 512 ticks

| Generator | Resource | Immediate final | Delayed final | Difference [95% CI] | Growth-only change |
|---|---|---:|---:|---|---:|
| crater-lakes 256² | wheat | 6318.8 | 6330.9 | +0.19% [-0.14, +0.49] | +1.06% |
| crater-lakes 256² | wood | 8513.8 | 8468.8 | -0.53% [-0.91, -0.14] | -1.35% |
| crater-lakes 256² | algae | 6488.4 | 6486.4 | -0.03% [-0.12, +0.06] | -1.39% |
| islands 256² | wheat | 12186.8 | 12156.1 | -0.25% [-0.41, -0.08] | -2.91% |
| islands 256² | wood | 13634.0 | 13620.4 | -0.10% [-0.26, +0.06] | -0.48% |
| islands 256² | algae | 11042.4 | 11042.5 | +0.00% [-0.06, +0.07] | +0.10% |
| river 128² | wheat | 2433.2 | 2433.8 | +0.02% [-0.30, +0.35] | +0.29% |
| river 128² | wood | 2738.2 | 2722.8 | -0.56% [-0.99, -0.12] | -2.83% |
| river 128² | algae | 2418.7 | 2416.7 | -0.08% [-0.19, +0.02] | -6.05% |
| swamp 128² | wheat | 2171.2 | 2166.2 | -0.23% [-0.67, +0.22] | -1.47% |
| swamp 128² | wood | 3001.8 | 2978.6 | -0.77% [-1.31, -0.23] | -2.32% |
| swamp 128² | algae | 2600.1 | 2594.9 | -0.20% [-0.43, +0.03] | -3.90% |


### 4096 ticks

| Generator | Resource | Immediate final | Delayed final | Difference [95% CI] | Growth-only change |
|---|---|---:|---:|---|---:|
| crater-lakes 256² | wheat | 12414.6 | 12474.3 | +0.48% [+0.14, +0.85] | +0.82% |
| crater-lakes 256² | wood | 23222.3 | 23100.2 | -0.53% [-1.26, +0.21] | -0.68% |
| crater-lakes 256² | algae | 7438.0 | 7444.8 | +0.09% [-0.14, +0.34] | +0.62% |
| islands 256² | wheat | 16796.0 | 16789.3 | -0.04% [-0.19, +0.12] | -0.12% |
| islands 256² | wood | 19254.1 | 19243.0 | -0.06% [-0.22, +0.10] | -0.13% |
| islands 256² | algae | 11724.0 | 11731.0 | +0.06% [-0.09, +0.20] | +0.89% |
| river 128² | wheat | 3360.6 | 3344.4 | -0.48% [-1.00, +0.05] | -1.45% |
| river 128² | wood | 4248.9 | 4250.0 | +0.02% [-1.01, +1.24] | +0.05% |
| river 128² | algae | 2637.3 | 2638.7 | +0.05% [-0.29, +0.43] | +0.56% |
| swamp 128² | wheat | 3818.9 | 3852.9 | +0.89% [+0.19, +1.55] | +1.72% |
| swamp 128² | wood | 6867.5 | 6828.1 | -0.57% [-1.56, +0.44] | -0.81% |
| swamp 128² | algae | 3452.1 | 3445.1 | -0.20% [-0.60, +0.23] | -0.71% |


Uncertainty uses100000 paired seed-bootstrap draws per map/resource, percentile95% intervals of ratios of means. Intervals are not multiplicity adjusted. Exact paired sign tests with Holm correction across12 comparisons at each horizon are also reported; they test consistency of direction, not mean magnitude. Do not count owner/shared or ticks as extra independent samples. This establishes neither exact ecological equivalence nor behavior under harvesting/AI. Cross-platform determinism was not assessed; Linux x86-64/GCC15.2 only.

At4096 ticks all twelve mean final-stock differences lie between−0.57% and+0.89%; growth-only changes lie between−1.45% and+1.72%. Most95% intervals include zero. Crater Lakes and Swamp wheat have positive unadjusted intervals, but no directional sign test remains significant after the12-comparison Holm correction (minimum adjusted p≈0.497). This is no evidence of a consistent large stock deficit in this sample, not proof of exact equivalence. At512 ticks the largest growth-only difference (River algae−6.05%) is only two stock units on average; final stocks differ by−0.08%. Absolute amounts and starting stocks matter.

## Reproduction and evidence

Build/dependency/executable/input hashes are in freeze.json; full compiler/link flags are in build logs and test provenance. The first512 run used the same harness before adding the optional tick override; its binary/source hashes are in512-build.json. The default remains512 for expanded output; the longer run adds only the environment override. The final source renames misleading `-playable` artifacts to `-original-colonies`, guards bare output filenames and asserts all three traditional materials are present. These test-only changes do not alter the simulated world;4096-build.json pins the expanded-run binary, and the matching harness source is archived. Both expanded datasets also independently satisfy the new presence assertion. Final smoke validation covers the updated harness. Build failures and the initial input-stream seek failure are preserved alongside passing reruns.

Run from the repository root with `LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib`:

```
python3 test/run_tests.py --binary engine --no-display -j1 --timeout 600 --filter 'ResourceGrowthBenchmark/generated*'
GLOB2_GROWTH_GENERATED_OUTPUT="$PWD/artifacts/resource-growth/remaining/generated-maps/results.json" python3 test/run_tests.py --binary engine --no-display -j1 --timeout 1800 --filter 'ResourceGrowthBenchmark/generated*'
GLOB2_GROWTH_GENERATED_TICKS=4096 GLOB2_GROWTH_GENERATED_OUTPUT="$PWD/artifacts/resource-growth/remaining/generated-maps/long/results.json" python3 test/run_tests.py --binary engine --no-display -j1 --timeout 1800 --filter 'ResourceGrowthBenchmark/generated*'
python3 artifacts/resource-growth/remaining/generated-maps/analyze.py artifacts/resource-growth/remaining/generated-maps
python3 artifacts/resource-growth/remaining/generated-maps/analyze.py artifacts/resource-growth/remaining/generated-maps/long
python3 test/run_tests.py --binary engine --no-display -j4 --filter 'ResourceGrowth/*' --filter 'ResourceGrowthBenchmark/player-free*'
python3 test/check_sim_revision.py --base origin/master
```

Smoke test, both expanded campaigns and18 existing growth/accounting cases pass. The sim-version gate passes. Full logs/JUnit and raw per-seed checkpoint data are included; independent reviewer findings are recorded in the PR. The final-queue flush and absent harvesting must be kept separate from ordinary gameplay conclusions. Latest-master integration and reserved-core performance remain outstanding on the draft PR.
