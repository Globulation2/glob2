# Exact merged-master verification checkpoint

Source: `43b153739838207983c922ab4cbeed7773459de9`, clean tracked tree. The merge tree equals the tested final PR candidate; receipt included.

Fresh local release verification on Ubuntu 24.04 x86_64 / GCC 13.3 / pinned SDL 3.4.16 and glslang 15.1: all 1,872 native registry jobs, 2,968 cases passed with zero failures, errors or skips. Fullscreen/display and headless cases included. Raw stdout, JUnit, build provenance, selected per-tick checksum traces and Castor/Maxima save-continuation artifacts are attached in the archive. The complete 6.5 GB native capture directory remains local; selected relevant evidence is public here.

Platform: pinned lockfile, Node 24.19.0, existing design-system revision acd277710acfebf528e1fc6f653d55fdf75de4b2. Design sync, lint, typecheck, i18n, web build, all 1,150 tests (15 existing skips), migrations and status passed. Tests used an isolated PostgreSQL 16 container pinned by digest; it was removed afterward. Commands and raw logs included. Hosted CI provides its configured Node 22 coverage.

Music: 135 tests, OK, six optional-extra skips. No optional model packages installed. Exact commands, flags and binary hashes are in verification.json and orchestrator scripts. Development, packaging and runtime dependency pins were not upgraded for these checks.

Full 221-case Chromium verification of newly built serial/threaded release WebAssembly on this exact source is STILL RUNNING at this checkpoint. Full current-policy hosted development verification is also pending: [manual full run](https://github.com/Globulation2/glob2/actions/runs/38916805749), [automatic master full run](https://github.com/Globulation2/glob2/actions/runs/38916585211). This checkpoint does not claim either result or hosted cross-platform success. Previous platform-specific evidence remains in this branch; final current-source hosted results will supersede pending status.
