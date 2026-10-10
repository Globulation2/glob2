# CLI 2 PR verification evidence

Tested source: `d4aede83ab474137d808afbdf1cac08cfd683e72`; PR base: `95016b53e3281d74940730ca2fa0be90c9e5c50d`. PR: [Globulation2/glob2#1051](https://github.com/Globulation2/glob2/pull/1051).
This evidence branch is separate from the implementation and is not merged into master.

Linux x86_64, GCC 15.2.0 (Ubuntu 15.2.0-16ubuntu1), Python3.14.4, Node22.22.1,
repository-pinned native SDL/codec dependencies. Builds use fast-development
`dev_fast=1`, `-O0 -g1`; no release readiness or performance claim.

## Exact final commands

```sh
GLOB2_SDL3_PREFIX="$PWD/artifacts/cli/native-sdl" python3 tools/dev_build.py engine-tests unit-tests build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2
python3 tools/dev_build.py target=web web_variant=serial
python3 tools/dev_build.py target=web web_variant=threaded
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --filter 'CommandLine/*' --filter '*InviteLink*' --filter 'Hive*' --no-display --artifacts artifacts/cli/final-reviewed-contracts --junit artifacts/cli/final-reviewed-contracts.xml
python3 test/test_cli_smoke.py --binary build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 --artifacts artifacts/cli/final-reviewed-smoke --junit artifacts/cli/final-reviewed-smoke.xml
python3 test/test_map_cli.py build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2
python3 tools/cli_reference.py --binary build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 --check
uv run --with-requirements tools/docs/requirements.txt python tools/check_docs.py
python3 test/test_tournaments.py
node --test browser/unit/*.test.js
```

From platform/: `npm exec -- vitest run packages/engine/test apps/engine-agent/test apps/skin-render-worker/test apps/api/test/coding-studio*`, `npm run typecheck`, and `GLOB2_BINARY="$PWD/../build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2" GLOB2_EVIDENCE_DIR="$PWD/../artifacts/cli/final-real-worker" npm exec -- vitest run apps/engine-agent/test/real-binary.test.ts`.

Browser tests serve each build with `python3 browser/serve.py PORT --bind 127.0.0.1 --directory BUILD`. From browser/, run `npm exec -- playwright test tests/cli.spec.js --output=SEPARATE_OUTPUT` with `GLOB2_TEST_URL` and `GLOB2_TEST_BUILD_DIR`; set `GLOB2_TEST_RUNTIME_PATH=/threaded` for threaded builds. The combined determinism test host stages the threaded runtime and both variants’ hashed data files together, then runs `npm exec -- playwright test tests/determinism.spec.js --project=chromium --grep='complete per-tick|verifies the committed' --output=SEPARATE_OUTPUT`.

## Results and rationale

- Fifteen real-production-binary tests pass, including every static help/JSON leaf without assets/display/profile/output creation, strict parsing, generated-document drift, job artifacts and saved continuation.
- Expanded map CLI:56 commands passed, including advertised preview/import options and data-directory resolution with unchanged preferences;21 diagnostic save/continuation commands passed.
- Native parser/invite/Hive contracts: 25 cases; 12 isolated jobs passed, one display-only Hive case skipped. CLI tests cover every definition and malformed/duplicate/conflicting/escaped options.
- Both browser variants: 24 static CLI tests each in Chromium/Firefox/WebKit. Six Chromium determinism/match tests pass. All four 1500-tick serial/threaded1/2/4 traces match native bytes.
- Platform:66 tests pass, eight optional real-binary tests skipped in that general run; the separate real worker suite passes five. Typecheck passes. Python tournaments43 and browser units87 pass. Documentation306/0errors;10 documentation-checker tests previously pass.
- Two native skin-rendering tests and an isolated real generator-worker test pass. Logs retain the tested environments and results. These earlier runs exercise unchanged rendering/sandbox boundaries; final changes concern validation verdict presentation and asset search paths covered by focused regressions.
- Bash/Zsh/Fish completions were syntax checked and invoked, including command, enum, path and help-prefix navigation. Man-page output was rendered with groff and inspected.
- GUI evidence exercises no-argument launch, explicit play/display settings, replay and finalized H.264 recording under Xvfb/Openbox. It covers unchanged GUI launch/recording behavior after review’s formatting and tool fixes.
- Fourteen affected Android sources passed pinned-NDK syntax compilation; configuration log records the full-link limitation.

## Equivalence

`merge-parity/` contains byte-equal maps, per-tick checksums, replay bytes, checkpoint/final saves, save continuation, catalog JSON and verification verdicts/checksums. The baseline executable reconstructs original command-handler objects from revision2c37ebf45 with unchanged simulation objects and the same native pins; it is an object-based baseline, not a separate pristine full build. Reproduction scripts and fixture/artifact logs are included. Random-game checksums and all three diagnostic exports also match. `SIM_REVISION` stays unchanged; no save/replay/network/domain JSON formats change.

## Limitations

No Windows/macOS builds or runtime, iOS compilation/runtime, OS desktop URL handoff, live production multiplayer or production deployment. macOS path separation is source reviewed. Android full link/runtime is blocked by the missing local pinned FFmpeg archive. Full release packaging and full cross-platform compatibility matrix were not run. Optional recording decoded-fixture tests and display-only Hive coverage remain omitted. Hosted checks may still be pending; cheap contracts do not establish full engine verification.

No production data was deleted. Binary and consumers require coordinated CLI2 rollout as documented in the CLI/deployment guides. Preserve historical data and bundles separately from active CLI2 workers.
