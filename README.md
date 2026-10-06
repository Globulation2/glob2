# PR 829 local verification

- Tested commit: 66c9250d46da0ca438aa51dfbbeb74ef9a9e1d11
- Base and integration: 5f090cbefb569f501d3709cd1389773c0a59e545 (PR head includes base; current master fetched before final acceptance, unchanged).
- OS: Ubuntu 26.04.1 x86_64; GCC 13.4.0, Clang 18.1.8, Node 22.22.1, Python 3.14.4. Hosted failing compiler was GCC13.3; local minor version differs.
- Dependencies: repository-patched SDL3.4.16/image3.4.6/net3.2.0/ttf3.2.2/WebP1.6.0, pinned recording stack and asset encoder. Manifests included. Local Postgres16 at127.0.0.1:55432 uses per-test isolated databases. No tracked source changes after tested commit; unrelated untracked build-software-terrain/ is preserved.

## Commands and results

Run from the checkout unless noted. ROOT is its absolute path. Exact standalone Clang command is in clang-object-command.json, extracted from the actual failing hosted compile and redirected to the same final source with the local generated includes/pinned dependency prefix. It compiled the actual AICortex.cpp with O0 coverage flags; nm now shows a defined weak symbol for SWARM_START_WORKERS (rather than an unresolved reference).

```sh
cd platform
npm run check
npm test -- --maxWorkers=4
```

The first check passed ESLint/Prettier and all three TypeScript builds, then failed tests because 104 default workers exhausted the LOCAL Postgres lock table (migration out-of-shared-memory). Its failure is retained in platform-check.log. No production/test configuration was changed: reran the complete same inventory with four workers, **723 passed,10 skipped,0 failed**,101 files passed/3 skipped, exit0. Optional tests retain their existing skips. platform-tests-four-workers.log preserves final counts. The original hosted regression was lint, not these local resource failures.

```sh
GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python GLOB2_SDL3_PREFIX=build/sdl3-ci-current/prefix scons CC=gcc-13 CXX=g++-13 -j16 release=0 server=0 --build=build/capability-tsan CXXFLAGS='-g -fsanitize=thread' LINKFLAGS='-fsanitize=thread' build/capability-tsan/src/glob2
LD_LIBRARY_PATH=$ROOT/build/sdl3-ci-current/prefix/lib GLOB2_SIM_THREAD=1 TSAN_OPTIONS=halt_on_error=1:exitcode=66:second_deadlock_stack=1:suppressions=$ROOT/test/tsan.supp build/capability-tsan/src/glob2 --run-game --map-file $ROOT/maps/balanced.map.gz --game-seed 7 --player maxima --player cortex --player warrush --player cabino --ticks 600 --replay true --output-dir $ROOT/artifacts/capability-ci-repair/tsan-headless
```

Full GCC13 TSan native build exit0, including the final game link. Same CI threaded headless smoke test completed **600 ticks,99 orders,checksum49808c0b,exit0**, with no ThreadSanitizer warning. Logs, result.json and produced replay included. No system memory-layout setting was altered. Clang coverage actual AI object exit0 and defined constant symbol are recorded in clang-object.log.

## Coverage, failures and limits

[Platform lint failure](https://github.com/Globulation2/glob2/actions/runs/37442302378/job/112199931041), [TSan link failure](https://github.com/Globulation2/glob2/actions/runs/37442302378/job/112199931773), [Clang coverage link failure](https://github.com/Globulation2/glob2/actions/runs/37442302378/job/112199930854). Extracts included; canonical hosted logs retain full evidence. Clang/GCC both ODR-used the static const integer through std::min; constexpr gives it inline storage without changing value4 or the order logic. No simulation computation, saves/replay/network or game-feel changes; no SIM_REVISION bump. The platform resolver returns one result for each input version, so the checked original-version fallback preserves successful catalog resolution and routing while satisfying lint.

Platform tests cover matchmaking, AI ratings/catalog identities and broader DB contracts. Full sanitizer link + actual Clang object specifically cover the observed unoptimized link failure; threaded game smoke exercises the changed AI class. No full native engine/unit suite, full Clang coverage run, Windows/macOS/Android/browser runtime matrix or cross-platform checksum comparison was reproduced locally, because the repair changes constant storage/lint handling rather than computation. Hosted affected checks requested via ci:run; subsequent full master will confirm recovery. This evidence does not claim the feature integration itself has complete cross-platform compatibility or fixes the historical Firefox startup flake.

Maintainer acceptance: Codex, under the author's explicit authorization and AGENTS.md, accepts these focused checks for this exact revision/base. Retain branch protections and monitor full master asynchronously.
