# Direct snapshot renderer: PR #908 evidence

This dedicated evidence branch is not application source. The implementation is reviewed for merge at https://github.com/Globulation2/glob2/pull/908.

`correctness.tar.gz` contains native test reports, exact commands and executable identities, deterministic replay/match traces, browser results, matched terrain images, and the focused memory-check report. `correctness-manifest.json` lists every archived file with its SHA-256.

The refreshed native tests use `e8e6320bf71576142bf2e4d546953af755d947f1`, based on master `52e4d3dc0440e12ff65494ad3eb1681e9c2c0af3`: 102 headless groups passed (six display skips), and 28 display checks passed. Seven terrain comparison PNGs are byte-identical to the clean `4cb2ac058` control. The shared-publication and direct-renderer code was unchanged by the later phone-editor-only correction and documentation update; the archived earlier browser/native traces cover the shared runtime before those final edits.

Native build: GCC 15.2, Linux x86-64, release `-O3`, pinned SDL 3.4.16 / image 3.4.6 / ttf 3.2.2 / net 3.2.0. The exact flags, source identity and test invocation are in `delivery-native-10.json`, the build log, and compiled provenance. Build command:

```sh
CCACHE=1 GLOB2_SDL3_PREFIX=/path/to/pinned/sdl3/prefix \
scons -k -j2 release=1 server=0 optimized_assets=0 engine-tests unit-tests \
  build/linux/client/release/src/glob2
python3 artifacts/direct-snapshot/run-delivery-validation.py
```

The archive preserves paths relative to the project root. Extract into a separate evidence directory for inspection; the validation driver expects the repository root when reproducing tests.

## Limits and outstanding work

- The completed architecture comparison contains 162 runs (six counterbalanced triplets per fixture/core setting). TPS improves by median paired 0.72–10.61% against a60018a and 6.68–46.29% against 4c3f607. RSS increases 0.73–7.66%; dense CPU/tick increases 13.46–15.77%. Large two-core tick p99 increases 5.15% (95% bootstrap interval 3.75–7.38%). The user requested merge after these results; this does not satisfy the original zero-regression gate. Worker defaults and storage bounds remain unchanged.
- Paced comparisons were stopped at that merge request; final physical-GPU comparisons and worker tuning were not completed. Partial/pilot data are excluded from the reported architecture comparison. Reservation and governor cleanup succeeded.
- Threaded Firefox WebGL torus screenshots omit the canvas in the failing check, while sampled trace screencasts contain it. Serial Firefox WebGL and context recovery pass. The clean-master browser control build was stopped at the merge request; the threaded visual failure remains unattributed.
- Integration with master `a0a59e4a6` passed in isolated merge `68903e3af4262c3593230ed9815d51a5ee26d479`: 42 headless groups and 13 display checks, including updated skin materials and colony previews. `integration-912.tar.gz` retains the commands, source/merge identity, executable hashes, reports and visual artifacts. Benchmark inputs remain pinned to the earlier candidate; this independent rendering change was not mixed into the performance runs.
- Windows and Android native execution were not performed for this candidate. Chromium, Firefox and WebKit replay/match traces match Linux native; runtime suites cover serial and threaded browser execution (WebKit uses software rendering).
- No human gameplay review was performed in this session. The user requested merge; automated checks do not establish pacing or visual feel.

`performance.tar.gz` contains the completed raw runs, exact invocations, source/instrumentation identities, reservation/governor audits, summaries and chart. `fixtures.tar.gz` contains the three reproducible saves. Their manifests and `SHA256SUMS` identify every artifact. See [performance tables](performance-tables.md). Candidate benchmark runtime is 0a7ffde9; final e8e6320bf adds only a phone-editor snapshot read and documentation, outside measured paths.
