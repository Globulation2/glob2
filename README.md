# Software renderer review evidence

Evidence for [PR #496](https://github.com/Globulation2/glob2/pull/496). This branch is an evidence snapshot, not a code change to merge. Its parent retains the original measured candidate source (`3a7f4b430`); baseline source is `777d19e09`. The PR code branch rebases that work onto newer master and includes review fixes. Original measurements are labeled accordingly, not represented as measurements against current master.

## Reproduction

Build optimized software clients at those source revisions, using separate build directories and disposable user profiles. The original baseline uses the local profiling harness that the PR promotes to `test/SoftwareRenderBenchmark.cpp`; its exact original source is in `baseline-harness/SoftwareProfile.cpp`. On baseline source, cherry-pick the tooling-only commit `fbfdddf8f`, replace `test/SoftwareRenderBenchmark.cpp` with that original harness, and build `software-render-benchmark`. This changes tooling only, retaining baseline production renderer objects. Match the drawing-only measurement loop and warmup parameters described in the PR development reference. Capture the baseline before switching implementations.

```sh
scons release=1 server=0 opengl=0 software-render-benchmark engine-tests
python3 tools/software_render_benchmark.py \
  --baseline /path/to/baseline/SoftwareProfile \
  --candidate /path/to/candidate/SoftwareRenderBenchmark \
  --save /path/to/evidence/fixtures/checkpoint-6000.game.gz \
  --resolution 1280x800 --frames 240 --warmup 30 --repeat 7 \
  --output artifacts/software-renderer/comparison
```

The two additional native fixtures are tracked game files (`games/gd-small-2ai.game.gz`, `games/gd-archipelago.game.gz`); the larger late-game save is included here. Each JSON records commands, profile overrides, all samples and medians. Logs record scene populations, stage costs, elapsed distributions, cache occupancy and backend operation counts. Paths in original logs describe the measurement host; relocate them to the downloaded fixtures.

Apple M3, macOS 26.6.2, SDL2-compat 2.32.70 over SDL3 3.4.14. Concurrent development builds add noise: use distributions and alternating medians, not individual samples. Original candidate transformed medians improved 7.87× / 6.92× / 11.24× (half / double / fractional offset). Four native fixtures improved 21–34%. Isolated terrain cache half-zoom improved 6.7%; visible presentation improved 3.4%. Smaller cache/presentation effects are less robust than the primitive benefit.

## Captures and correctness

`captures/` contains lossless PNG conversions of original first-pair BMP captures. All four native fixtures and the populated double-zoom reference have exact RGBA equality. Half/fractional differences reflect nearest-neighbor sampling and shared endpoint rounding; inspect alongside the pixel and scene tests and play the result. Enlarged owned water assets preserve the existing SDL large-triangle behavior.

`tests/` includes original focused software/OpenGL test results. Rebased follow-up results will be added separately. Linux/Windows runtime checks and maintainer playtesting are not claimed.

`continuation/` retains the replay, detailed checksums and final save of the original 128-tick saved-game continuation, compressed with gzip. The comparison log records equality against baseline. Gunzip replay/checksums before using the ordinary replay tools. Baseline and candidate hashes:

- Replay: `2bd83ac28e9cbfcb66659ce1aa564b270bc282160641f0c85695f5bc05508df3`
- Detailed checksums: `67375cb699317b61285dd70676b881f75c76b7b9c56ffb65fb842f5c2756c74f`
- Decompressed final save: `3e9cfdb5f620adde197bce3d8236adbfc8b7ebc4b019d69736ec57e3b094d2f3`

`original-verification.json` is the original development manifest. Some early intermediate phase measurements are retained there for traceability; use the completed cohorts in `profiles/` for accepted performance claims. Rejected experimental logs are excluded.

## Rebased follow-up validation

The candidate at `2253ec09d` fixes native-display workload drift by disabling HiDPI in the benchmark by default. Native-display lifecycle tests retain HiDPI. `profiles/pr-fixed-pixel-pairs` contains seven new alternating pairs against the original baseline with fixed 1280×800 framebuffers, 240 measured frames and 30 warmup frames. Concurrent debug builds add timing noise. Native and double-zoom first-pair captures are exactly equal.

- native: 3.2653 → 2.3529 CPU ms/frame (1.39×).
- half: 28.7060 → 4.9345 CPU ms/frame (5.82×).
- double: 14.5902 → 2.5059 CPU ms/frame (5.82×).
- fractional: 29.3126 → 3.0273 CPU ms/frame (9.68×).

Rebased checks: 23 focused software cases and 28 OpenGL-enabled cases passed. Final target-binding hardening recomputes native scaling for differently sized replacement surfaces; its targeted follow-up results are included when completed. Platform CI may still be queued; no Linux/Windows runtime equivalence or maintainer playtesting is asserted.

Final review fix at `9fdf99b8c`: differently sized native target replacement preserves scale agreement between direct and fallback drawing. Seven affected software lifecycle/portable checks and the OpenGL-enabled native target regression passed. The fixed-pixel benchmark path retains the measured behavior.
