# Snapshot presentation integration evidence

Source: 0588770be (stacked on 474681cd1, 773ad5942 and b9d45bf06).
Original base: 64406b082. Latest checked master: 6831dcfef; clean merge-tree, unrelated JavaScript save-evidence parser changes only.

See environment.json for Linux x86-64, GCC 15.2, dependency versions and build flags. No SIM_REVISION or save-format changes were made. The standalone scheduler harness was also compiled with GCC 13; its log is included.

## Commands

```
CCACHE=1 GLOB2_SDL3_PREFIX=/home/bradley/glob2-claude/build/sdl3/prefix scons -j8 release=1 server=0 optimized_assets=0 unit-tests engine-tests build/linux/client/release/src/glob2
python3 test/run_tests.py --binary unit --filter 'ComputeExecutor/*' --filter 'SceneBuffer/*' --filter 'FogFade/*' --junit artifacts/presentation/unit-final.xml
python3 test/run_tests.py --binary engine --filter 'ClientChannels/*' --filter 'WorldSnapshot/*' --filter 'SceneExtract/*' --filter 'SharedWorkerLifecycle/*' --filter 'GameGUISelection/*' --filter 'GUIInteractionCoverage/*' --filter 'GUIOrderCoverage/*' --filter 'LegacyScriptCoverage/*' --filter 'ScriptPresentation/*' --junit artifacts/presentation/engine-final.xml --artifacts artifacts/presentation/final-images
python3 test/build_system/test_scene_boundary.py
GLOB2_SYNC_RAND_STRICT=1 python3 test/check_sim_thread.py build/linux/client/release/src/glob2 --baseline build/linux/client/release/src/glob2 --candidate-env GLOB2_SIM_THREAD=1 --output artifacts/presentation/sim-thread-verified
GLOB2_SYNC_RAND_STRICT=1 python3 test/check_sim_thread.py build/linux/client/release/src/glob2 --baseline build/linux/client/release/src/glob2 --candidate-env GLOB2_SIM_THREAD=1 --candidate-args '--compute-threads 1' --output artifacts/presentation/sim-thread-fallback-verified
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/presentation/verify-final
cmp test/fixtures/multiplayer/FourSquares1.verify-trace.txt artifacts/presentation/verify-final/checksums.txt
```

## Results and scope

Final integrated checks: 86 engine cases, 16 unit cases, and 2 Scene boundary checks passed. The standalone GCC 13 harness passed 8 cases / 137533 assertions. The new rendering check produced pixel-identical snapshot and synchronous images for both ground and overlay custom buildings.

Simulation-thread comparisons passed new-game, new-game-maxima, generated-load, legacy-v121 and resume scenarios with both normal compute scheduling and one compute thread. Per-tick checksum sidecars, replay bytes and final saves match. These compare serial and threaded execution of the integrated candidate; they are not a comparison against a separately compiled base. The committed multiplayer golden trace matches byte for byte and the verifier returned verified at tick 701 with no rejected orders.

simulation-evidence.tar.gz contains default serial references and both threaded candidates, with saves, replays and checksum sidecars. simulation-manifest.json records SHA256 hashes for every run, including the baseline repeats (duplicate baseline payloads are omitted from the archive). JUnit files list the actual selected cases. Render images are fixture captures, not an interactive gameplay performance measurement.

The focused tests exercise scheduler barriers and replacement, Scene buffer ownership, retained snapshots, paused edits, source lifetime, Scene query/pixel parity, selection generations, client FIFO/pulse concurrency, immediate script queries, GUI interaction/order handling and legacy scripts. No Windows, Android, browser, cross-compiler full-engine equivalence, sanitizer run or before/after performance benchmark is claimed. Interactive pacing still needs play review.

These are draft staged changes. GUI/input still park the simulation for live reads; native turn and browser threading have not been enabled. This evidence does not establish completion of the full rendering separation plan.
