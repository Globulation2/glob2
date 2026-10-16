Verification for PR #1199, commit f881f856101b72bb40d30cacbc3bb4e835906da1; base e04711348d27238e3f21c3ea42179e383ac422ec.

Windows full-master job https://github.com/Globulation2/glob2/actions/runs/38861165281/job/116846313115 fails because Windows headers define the local identifier `far` as an empty macro. The repair only renames that local to `outerHalf`.

Ubuntu 24.04 x86_64, GCC 13.3.0, existing pinned SDL 3.4.16 headers. Exact compiler commands, include paths and flags are in the *-result.json files: -std=gnu++20 -Wall -fPIC -O3 -s -fno-fast-math -ffp-contract=off.

- Original translation unit plus `-Dfar=` reproduces the same four Windows errors (expected exit 1).
- Renamed translation unit plus `-Dfar=` compiles (exit 0).
- Ordinary renamed compilation succeeds. Original, renamed and macro-enabled objects are byte-identical: SHA256 42ddff4b7c8fe9a28e90d7a03111bcb6500c5019be8eebe294947f7a39360e8b. Actual object files are retained for `cmp` verification.
- `GLOB2_SDL3_PREFIX=<pinned-prefix> GLOB2_GLSLANG=<pinned-glslang> scons --build=build/ci-recovery-release release=1 -j6 map-generator-golden-test`: passed.
- `build/ci-recovery-release/src/MapGeneratorGoldenTest <profile> --print --only=bastion-keys`: all eight rows exactly match the accepted table, including two expected invalid requests.
- `python3 test/check_sim_revision.py --base origin/master`: passed.

Coverage rationale: this is an identifier-only preprocessing repair. Byte-identical objects and unchanged golden cases establish that the recipe, random draws, terrain and simulation are unchanged, so no generator or simulation revision bump is appropriate. The original repair's full native and compatibility evidence remains applicable. Actual Windows compilation is being checked by focused run https://github.com/Globulation2/glob2/actions/runs/38864320252; full master CI remains the final hosted recovery check. No complete Windows success is claimed here.
