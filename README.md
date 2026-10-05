# Windows replay export fixture verification

Head a026fd3f698836871318b3c5187695707564f810, base104384a52c94dc5a06d82a9acb16badcad9e304f. Ubuntu26.04.1 x86_64 GCC15.2/Python3.14.4; SDL3.4.16/image3.4.6/ttf3.2.2; release software opengl=0. Native Windows/Wine/cross toolchain unavailable locally.

Hosted Windows failure in master104 run37289559297 job111708305409 records atomic replacement failure for live.replay, followed by writer.write(same recording path) assertion failure. Keep the writer's open recording separate from its exported file; this is test-only and changes no platform file semantics, production code or simulation.

Build: GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix GLOB2_RECORDING_PREFIX=build/linux/client/release/recording/prefix GLOB2_ASSET_ENCODER_PYTHON=<managed encoder venv>/bin/python scons -j12 release=1 server=0 opengl=0 --build=build-software-terrain engine-tests. Exit0; exact commands/flags and tested source provenance retained in logs.

python3 test/run_tests.py --build-dir build-software-terrain --binary engine --filter 'SavegameSafety/*' --junit artifacts/windows-replay-export/junit.xml --artifacts artifacts/windows-replay-export/tests --verbose: 1PASS10.3s. Both current writer live/finished exports import; truncated footer, corrupt magic and trailing bytes reject. Existing save/load, imports, atomic/background writes and campaign checks retained.

Production untouched; focused fixture suite appropriate. Windows confirmation and full master recovery await subsequent hosted CI. Broader engine/browser/platform tests omitted because only test export destination paths change. Fetch latestmaster before merge; no overlapping source changes at final verification.
