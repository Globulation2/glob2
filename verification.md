Local / VM verification

- Tested commit SHA: 7988af82d5215f3db5f95e7d91e71ee0fdae4a1f
- Base revision: 3d896cbb95293da6d24caddd247a62c9ca99d77f; PR head rebased onto this master revision, no merge conflicts remain.
- Environment: Linux 7.0.0-31-generic x86_64 GNU/Linux; c++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0; system SDL3 3.4.2; repository doctest 2.4.11. No dependency changes.
- Build configuration: direct focused harness build, C++20, -O0 -g1 -pthread, no PCH/unity. See reproduce.sh for exact commands.
- Coverage: native ComputeExecutor and ApplicationResourcePlan suites, including physical-core sizing, affinity bounding, logical/hardware fallbacks, unknown counts, explicit overrides, threading unavailable, oversubscription provenance, executor barriers and deferred-work lifecycle. All 61 cases passed (138408 assertions).
- Browser policy: native-host compilation with __EMSCRIPTEN__ enabled, using the first three committed ComputeExecutor sizing/configuration cases plus the full ApplicationResourcePlan suite; all 14 cases passed (205 assertions). This is a preprocessor/policy check, not browser or Wasm execution.
- Routine build command: python3 tools/dev_build.py unit-tests. Exit 1 during dependency configuration: missing SDL3_ttf, SDL3_image and SDL3_net. Full configuration log attached.
- Focused build and test commands exited 0. git diff origin/master --check exited 0.
- Omitted: full client/unit binaries (missing SDL extension dependencies), Windows/macOS/Android/iOS/browser runtime builds, cross-platform replay checksums, save continuation, and physical one-/two-/three-core performance or play testing. No speedup claim is made. Auto sizing only selects participant counts already supported by explicit overrides; simulation calculations, saved state and publication deadlines are unchanged. No simulation-version bump.
- Behavioral effect: on detected 1/2/3 available CPUs, total participants become 2/3/4; 4+ retain the current count. Unknown CPUs retain serial. Physical topology remains preferred. Single-core auto now also satisfies the existing >1 participant compute-GPU permission gate; actual backend admission remains separate.
- Maintainer acceptance: pending review; draft PR.
