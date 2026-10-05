# Ecology rebuild verification

Tested source: 3266c8e518a0d40b2100e2e36596822ad74a6f0b. Integrated engine/game checks ran on 00ce558fe5601cb20db8cda229b15df1638c9e70; the final follow-up changes only the existing unit oracle dimension list. Tests rebuilt and all795headless unit cases reran successfully after that addition. Production source is identical. Base: 42848408e (full revision in final-provenance.json). Source changes were tested before committing. Initial and final binary/source hashes are attached; the final follow-up has no production change. Master fetched before final verification; no intervening changes. Linux x86_64, GCC 15.2.0; source/binary hashes attached.

Build (exit 0):

    GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX=/tmp/glob2-terrain-baseline-build/recording/prefix taskset -c 0-11 scons -j12 release=1 tests build/linux/client/release/src/glob2

See build.log and build-final.log for exact compile/link flags and dependencies. Tests use LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib, the retained SDL_image runtime with the repository PNG16 patch; production SDK and recording dependencies were unchanged. Release is -O3 with production -fPIC; kernel comparisons use those same kernel flags.

Final coverage-only follow-up build: same SCons environment/flags with target `tests` (build-test-coverage.log), followed by the unit command below.

Commands (all exit 0):

    LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib taskset -c 0-7 python3 test/run_tests.py --binary unit --no-display --junit artifacts/ecology-optimization/unit.xml
    LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib taskset -c 0-7 python3 test/run_tests.py --binary engine --no-display --filter '*Terrain*' --filter '*Fertility*' --filter 'Maxima*' --filter '*MapQuer*' --filter '*MapTil*' --filter '*Save*' --filter '*TurnEngine*' --filter '*JavaScript*' --junit artifacts/ecology-optimization/engine.xml
    LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib python3 artifacts/ecology-optimization/compare-games.py
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib taskset -c 0-7 gdb -batch -x artifacts/ecology-rebuild/cache-audit/count.gdb build/linux/client/release/test/glob2-engine-tests

795 unit cases pass (17 display skips). 267 engine cases pass (15 display skips), covering terrain/cache lifecycle, warmed same-size save/load, tiling/import, AI farming/economy, JavaScript, save compatibility and committed match-record checksum golden. No fixture update or simulation revision bump: calculations remain identical. GDB on final binary: 512 growth ticks, 512 cache lookups, one rebuild, 1595 assertions pass.

Three generated maps at seed1427 (allotments128², canals256², river512²), Maxima versus Nicowar, game seed12345, each4096ticks: entire per-tick trace is identical between retained preoptimization production binary, current serial engine, and current four-thread/three-gradient-worker engine. Maps, input commands, binary hashes, trace files and result metadata are attached. The retained comparison binary is from the previous terrain-refactor build, not an exact fresh build of this PR base; baseline kernel sources ARE taken from this PR base. Therefore whole-game checks support unchanged execution against that retained binary, while exact-base kernel comparisons establish field equivalence directly.

Independent reviewer checked all three land paths and aquatic adaptive paths against an independent direct961-tap oracle on272cases, including thin/rectangular wrapping and full int16/uint16 representation bounds. Native ASan+UBSan and ARM64 QEMU match; scripts, commands and logs under ecology-rebuild/kernel-review. A pointer-padding intermediate-UB finding was fixed and both runs repeated on final source. Cache lifecycle and AI usage received separate independent review.

Performance: see KERNELS.md and paired-*-release.summary.json for final production-kernel-flag measurements, reproduction commands and memory accounting. Interleaved same-process nine-sample medians, CPU15; these measure land+aquatic field calculation, not whole-game FPS or total generation/load latency. Inputs and baseline/current output digests are included.

Limits: full-engine Windows/macOS/browser/Android and ARM64 replay checks not run; ARM64 coverage is direct kernels only. Display/visual checks omitted because no rendering behavior changed. Hosted expensive CI not requested. Performance is one Linux machine; warm gameplay already reused the fields, so no steady-state speedup is claimed. Save/replay/network formats and intended gameplay feel are unchanged.

Maintainer acceptance: focused local evidence accepted by author for PR review; no claim of human playtesting or cross-platform full-engine equivalence. Evidence branch is archival only and must not be merged.
