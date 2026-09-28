# AI save portability validation

This package validates the fixes in PR #383 against master `c464848726379f9f286c43452820919727e04078`. Exact source commits and compiler metadata are in `source.json`; `final.patch` contains the implementation and regression harness. No lazy-gradient changes are included: this is a separate correction of the issues found while validating PR #380.

## What changed

- Echo's saved alliance/vision integers and `update_gm` have deterministic initial values before the first AI tick.
- AddArea and RemoveArea consume x and y sequentially, preserving their saved order on Clang and GCC.
- Maxima no longer reads the nonexistent `policy_bids[6]`. Its twenty legacy telemetry columns remain in the schema for compatibility, but capture clears their current values to unavailable. Existing historical samples remain readable and are not silently rewritten.

The binary save layout and save/replay/network versions are unchanged. Correcting a transposed pending area order can change how an affected save resumes on older GCC builds; this is intentional. The focused harness executes restored orders at asymmetric coordinates and checks that the intended tiles, rather than the transposed tiles, are changed. The full-game scenarios below do not exercise every possible pending order or promise identical results to buggy loaders in all games.

## Verification

`AISavePortabilityHarness` passes on macOS arm64 / Apple Clang 21, Linux x86_64 / GCC 15.2 (therig), and Linux x86_64 / GCC 13.3 (devlaptop). It checks initialization, AddArea and RemoveArea binary round trips and actual execution, loading old telemetry samples, clearing nonexistent-policy columns, and sampling the real sixth policy. Linux CI builds and runs it.

The retained version-115 Maxima checkpoint matches all 512 expected current-policy team/entity state hashes. Saving/reloading again at tick 30256 preserves all 256 continuation records. The replay reader's floor/ceiling and decode-version tests pass. Their logs are included.

The full-game matrix uses the same two 256² four-AI maps as the issue-discovery run: Arena seed 73 and Crater Lakes seed 131, game seed 31, Nicowar/Warrush/Cortex/Maxima, 8192 ticks each. A further 4096-tick continuation loads the exact same original-master checkpoint from the earlier gradient evidence on all hosts. The input is retained; it contains the coordinate list that exposed the compiler-dependent load order.

All **61,440 baseline-versus-fixed tick pairs match** (20,480 scenario ticks on each of three hosts). Every corrected initial/checkpoint/final save and checksum sidecar is **byte-identical across all three platforms**, including the previously mismatching continuation saves. This is 18 full-game executions / 9 paired comparisons. The raw files support this claim without filtering or normalization.

`comparison.json` is the authority for every literal file comparison. `baseline_vs_fixed` compares each host's master and fixed run, while `fixed_cross_platform` compares corrected Linux output to corrected macOS. Save differences against master are expected because the fixes change undefined diagnostic values and correct coordinate loading. No normalization is applied.

## Evidence and reproduction

`manifest.json` maps every compared save and checksum sidecar to its original byte count and SHA256. Identical files are stored once as `objects/<sha256>.gz`; decompression returns the unmodified bytes. `materialize.py` restores the paths used by `compare.py`, verifying every hash. The inputs are uncompressed map/save files.

Per-host results and plans retain exact commands, output hashes and binary hashes. Build each source with `scons release=1 server=0 ai-save-portability-test build/<toolchain>/client/release/src/glob2` (darwin or linux). Run the harness through `test/run-savegame-safety-tests.py` with the source directory argument. GCC builds used -j16 on therig and -j8 on devlaptop; macOS used CCACHE=1. Files were exported into isolated source directories, preserving the hosts' working repositories.

To reproduce games, place `cases.json` and `inputs/` in a fresh evidence root, then run:

```
python3 run-games.py --root <evidence-root> --base <master-binary> --head <fixed-binary> --base-cwd <master-source> --head-cwd <fixed-source>
```

The runner uses dummy SDL drivers and clears GLOB2 environment overrides. This is correctness validation, not a new timing study. Interactive play and Windows/WebAssembly full-game workloads are not part of this local matrix.
