# CI bootstrap and Maxima policy repair evidence

Tested head: `6198cab3631b23c7274147c6e16fcfc7f40bc3ad`; fetched base: `d80e39b1521aeef0097df3a0e350748f33a2755f`.
Environment: Ubuntu 26.04, Linux x86_64, Python 3.14, GCC 15.2 development build (-O0 -g1).

Hosted failure logs are 122351669834.log (APT installation timeout), 122359119865.log and 122359229773.log (Maxima structural guard).

## Validation

- `python3 -m unittest discover -s test/build_system -p 'test_ci*.py'`: 163 tests, zero failures, 2 skips (ci-contracts-qualified.log).
- `GLOB2_BUILD_DIR=build/linux/client/debug/dev-dev_fast-true-linker-auto python3 -m unittest discover -s src/ai/maxima -p '*Test.py'`: all 75 pass (maxima-qualified.log).
- `python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'Maxima.Implementation/strategy and maintenance share the deterministic queue' --filter 'Maxima.Implementation/director execution regressions' --junit artifacts/ci-watch/1801/director.xml`: both pass (director.log and XML). Native implementation is unchanged from the previously validated recovery build; no simulation sources changed since that build.
- `GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix python3 tools/dev_build.py maxima-strategy-dump`: pass (build-dump.log).
- `python3 tools/check_docs.py`: 310 documents, zero errors (docs.log). Python compilation and git diff --check pass.
- Before-fix Python and shell-fixture failures: maxima-before.log, mirrors-before.log. Shell regression executes all four real workflow rewrite blocks, redirecting filesystem paths into isolated fixtures.
- Policy guard mutation checks reject removing the dirty trigger, moving refresh past scheduled executors, and removing evaluation from check_phases (policy-mutations.log).
- apt-network includes actual rewritten source/mirror configuration, exact commands, logs and downloaded package. Signed noble indexes and package download succeed over HTTPS. Initial download selected the host's newer installed version; retry isolates the status file so the noble candidate is selected. No host APT sources or package state were modified.
- runner-configure-apt-source.json is the official actions/runner-images source response describing /etc/apt/apt-mirrors.txt, with blob SHA and source URL.

## Limits

No full prerequisite install, hosted matrix checkpoint, shipping/performance measurement or cross-platform execution is claimed. No checks, selected cases, runtime dependencies, simulation code or golden fixtures change. The author accepts this focused evidence under repository repair policy and the user's merge authorization. Active master CI is allowed to finish.
