Fullscreen restoration verification

Source 3394f1aef (full SHA in verification.json), base e1584c5a26af3dced8852c88dc64919931658a6b. Ubuntu 24.04 x86_64, GCC13.3, release=1 (-O3 -s), existing pinned SDL3.4.16/codec dependencies. No simulation, saves or dependency changes. Source and exact compile/link commands/provenance are retained.

The full merged-master native run completed 1872 jobs/2968 cases: 1869 jobs passed, three failed. Two UIPresentation viewport failures inherited the real desktop DISPLAY=:1, which constrained window dimensions below the declared virtual-screen sizes (1854x1011 instead of 1920x1080, 768x1011 instead of 768x1024). These do not establish layout bugs; final validation unsets DISPLAY so the registry owns correctly sized Xvfb sessions. The third failure is real fullscreen restoration: callback 1280x603 overwrote the remembered 1280x640. Isolated follow-up also reproduced it in OpenGL.

The repair waits for SDL's fullscreen-exit operation to settle before requesting the saved client dimensions, then waits for that resize before reading/persisting display preferences. On a failed first synchronization it follows the existing rollback path. This avoids overlapping the client resize with restoration of window-manager decorations. Intended visible effect: returning from fullscreen/F11 preserves the original window dimensions rather than occasionally shrinking it. Existing context identity, rendering pixels, clipping, input, callback and layout-floor assertions remain.

Validation:
- Baseline stress at e1584c5a2: 19/20 native software/OpenGL transitions passed, one reproduced the size corruption.
- Candidate composition (same baseline objects/test support, only rebuilt libgag archive containing the patch; link command retained): all four resize cases passed; 40/40 transitions passed with test processes limited to CPUs 0,1. Candidate embedded TestMain provenance names the baseline, so these are diagnostic results, not claimed as clean final binaries.
- Clean release rebuild at 3394f1aef: engine-tests and unit-tests passed compilation.
- env -u DISPLAY python3 test/run_tests.py --build-dir build/ci-recovery-release --filter 'WindowResize/*' --filter 'FullscreenAspect/*' --fullscreen --display-jobs 1 --junit <retained>/clean-tests.xml: all six cases passed, zero failures/skips. Actual Xvfb 1600x1400/Openbox, Mesa software OpenGL. Covers software and GL fullscreen/F11, live scale, remembered size, context lifetime, cache behavior, clipping and input.
- python3 tools/check_docs.py: 309 documents/zero errors.
- python3 test/check_sim_revision.py --base origin/master: passed.

A complete native rerun at the clean source is in progress with DISPLAY unset, eight workers/two display jobs, every case retained. Prior complete native and cross-platform simulation/save evidence remains applicable; only presentation request ordering changes. Actual macOS/Windows fullscreen behavior cannot be verified on this Linux host and remains full-master CI coverage. No performance claim is made. No unrelated UI layout or runner behavior changes are included.
