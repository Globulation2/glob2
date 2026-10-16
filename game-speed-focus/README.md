Pause fixture focus verification for PR #1204

Final source ae56c3b2de6f83c2392358a05c4459b963000b40; base b115b5942ece98256ee690dac10b1af58e59de90. Ubuntu 24.04 x86_64/GCC13.3, release=1 (-O3 -s), existing pinned SDL 3.4.16 and codec dependencies. Exact compiler/link commands and clean source provenance are retained. No production, simulation, save or dependency changes.

The isolated-display full native run at clean 3394f1aef (tree identical to merged b115b5942) completed 1872 jobs/2968 cases: 1871 jobs passed, only GameSpeed/live engine speed; pause; hard pause and replay playback failed. The hard-pause probe ended after 36ms, before its 240ms resume timer. A separate follow-up failed the regular pause probe after 30ms. Three subsequent uncorrected stress repetitions passed, so this is intermittent rather than a deterministic speed failure.

The old fixture queues its initial shortcut before starting an uncapped threaded producer, so its 50 ticks can finish before the first client input frame. Native focus notifications can also invalidate synthetic input. The repair uses the public incremental session API to apply and assert the initial paused state before starting the producer. The resume remains a real 240ms SDL timer event. A bounded host loop then runs the same client/threaded or serial APIs and finalizes the session. It supplies focus with each synthetic key and removes its timer safely. All existing lower/upper timing bounds, regular/hard pause checks, replay speed comparisons and seven final checksum comparisons remain. Production code is unchanged.

Validation:
- Diagnostic candidate (only the test object replaced in the prior complete release link) passes the three GameSpeed cases and 10/10 complete live probes with processes restricted to CPUs 0,1. Its embedded TestMain provenance still names 3394f1aef; those results are explicitly diagnostic compositions, not clean final builds.
- An initial clean focus-only candidate at ba8461519 still failed (29ms), despite passing constrained-CPU diagnostic runs; that result is retained and is not accepted as a fix.
- Clean final source ae56c3b2de6f83c2392358a05c4459b963000b40: GLOB2_SDL3_PREFIX=<existing-pinned-prefix> GLOB2_GLSLANG=<pinned-glslang15.1> scons --build=build/ci-recovery-release release=1 -j6 engine-tests unit-tests passes.
- env -u DISPLAY python3 test/run_tests.py --build-dir build/ci-recovery-release --filter 'GameSpeed/*' --fullscreen --display-jobs 1 --junit <retained>/paused-tests.xml: all three cases pass, zero failures/skips. Ten further clean final live probes pass with default threading and three pass with GLOB2_SIM_THREAD=0, all under owned Xvfb sessions without CPU affinity restrictions.
- python3 test/check_sim_revision.py --base origin/master passes.

Coverage rationale: a fixture-only event precondition, leaving all assertions and shipped code unchanged. The complete prior suite passes every other case, including fullscreen restoration and all viewport sweeps under isolated Xvfb. Full final local rerun and master CI continue. Actual Windows/macOS execution is not claimed locally; no gameplay or artwork feel changes. Focused success is not a full hosted checkpoint.

Final local checkpoint: the complete run at 61cd188ecf8d50b229e21539bb9e62cf12f745e4 passes all 1,872 jobs / 2,968 cases with no failures/errors/skips; final fixture-only clock-order follow-up passes all three affected cases at de693a01b7a4e2b304a86cce2d6b28c44d3b2314. See final-verification for exact revisions, tree equality and pending hosted status.
