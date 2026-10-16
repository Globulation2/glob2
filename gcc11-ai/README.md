Verification for PR #1200, source 63807e0c82ede06fe0652e0bd182e625bfc88e37; base c16cf007512255e378ed3b92395e4eb2d01f7b38. Master fetched before final validation; unchanged.

The full master GCC 11 build and Ubuntu 22.04 software-only build fail at `HeadlessGame restored({})`: the empty list is ambiguous with the deleted copy constructor. Default construction calls the same options constructor using its existing default argument.

Ubuntu 24.04 x86_64, GCC 11.4.0 and GCC 13.3.0, pinned SDL 3.4.16 headers. Exact flags/include paths and compiler exit codes are in *-result.json. The old source reproduces the GCC 11 error (expected exit 1); the fixed source compiles with GCC 11 (exit 0). GCC 13 before/after test objects are byte-identical (object-comparison.json).

`GLOB2_SDL3_PREFIX=<pinned-prefix> GLOB2_GLSLANG=<pinned-glslang> scons --build=build/ci-recovery-release release=1 -j6 engine-tests` passed.

`env -u DISPLAY python3 test/run_tests.py --binary engine --build-dir build/ci-recovery-release --filter 'AIPipeline/*' --no-display -j6 --junit <tests.xml> --artifacts <tests>`: all 11 cases passed, zero failures/skips, including legacy receipts, save continuation, controller worker counts and pipeline delays. JUnit and logs retained. Build provenance is clean at the exact tested source. The sim-version checker passed.

This change affects one test fixture construction expression; production code, test assertions and runtime behavior are unchanged. Full hosted GCC 11 build/execution and shard audit remain the subsequent master-CI confirmation. No full GCC 11 runtime pass is claimed from the local GCC 11 translation-unit compile; local execution used GCC 13.
