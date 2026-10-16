Pause measurement ordering verification

Final source de693a01b7a4e2b304a86cce2d6b28c44d3b2314, base a3f118590f78dc653d7a2fb1d444a5b6d0dd9441. Ubuntu24.04 x86_64/GCC13.3, release=1/-O3 -s, existing pinned SDL3.4.16/codecs. Only the fixture's measurement start moves: it is read before SDL_AddTimer rather than after. All timing bounds, initial-pause precondition, normal/hard-pause and seven checksum checks remain. Production code, simulation, saves and dependencies are unchanged.

A scheduler delay after arming the timer can consume its 240ms wait before the measurement starts. Deterministic diagnostic: inject SDL_Delay(300) immediately after the timer is created in an ignored fixture copy. The old ordering fails, measuring 97ms; the corrected ordering passes the complete live pause/replay case with all checksum checks. Diagnostic executables link unchanged production/test-support objects from clean 61cd188ec with only the copied fixture object replaced. Their embedded TestMain provenance still names 61cd188ec; source copies and exact compile/link recipe are retained, and these results are not represented as clean final binaries.

Clean final validation:
- GLOB2_SDL3_PREFIX=<existing-pinned-prefix> GLOB2_GLSLANG=<pinned-glslang15.1> scons --build=build/ci-recovery-release release=1 -j6 engine-tests unit-tests: passed at de693a01b7a4e2b304a86cce2d6b28c44d3b2314, clean source.
- env -u DISPLAY python3 test/run_tests.py --build-dir build/ci-recovery-release --filter 'GameSpeed/*' --fullscreen --display-jobs 1 --junit <retained>/clean-tests.xml: all three cases pass (6.0s), zero failures/skips, under owned Xvfb/Openbox.
- python3 test/check_sim_revision.py --base origin/master: passed.

The complete immediately preceding local run at clean 61cd188ecf8d50b229e21539bb9e62cf12f745e4 (tree identical to base a3f118590) passes all 1872 jobs/2968 cases, zero failures/errors/skips. Its log/JUnit and the focused final-source follow-up are retained. The broader inventory is unchanged; repeating all unaffected cases after this fixture-only timestamp ordering change is unnecessary. Full latest-master hosted CI continues. Actual Windows/macOS execution is not claimed locally; no production behavior or performance change.
