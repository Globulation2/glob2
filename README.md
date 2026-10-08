# Direct snapshot renderer: PR #908 evidence

This dedicated evidence branch is not application source. The implementation remains a draft at https://github.com/Globulation2/glob2/pull/908.

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

- Performance acceptance, paced/GPU comparisons and worker tuning are still in progress; no performance conclusion is asserted here.
- Threaded Firefox WebGL torus screenshots omit the canvas in the failing check, while sampled trace screencasts contain it. Serial Firefox WebGL and context recovery pass. A clean-master browser control is being built; the threaded visual failure is not yet attributed.
- Current-master integration with the later experimental skin-material update is being checked in an isolated merge checkout.
- Windows and Android native execution were not performed for this candidate. Chromium, Firefox and WebKit replay/match traces match Linux native; runtime suites cover serial and threaded browser execution (WebKit uses software rendering).
- A maintainer gameplay review is still required for animation, pacing, input and visual feel. Automated checks do not replace that review.
