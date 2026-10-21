# Translation validation

PR: https://github.com/Globulation2/glob2/pull/1243

Source revision: `d1621b1c8485943ad75372303e918732584db4fb` (clean committed working tree). Integrated current master `b6ff1df8c` after Settings alignment PR #1244 landed. Resolved duplicate English keys, redundant empty locale entries and pending entries in favor of the reviewed complete translations; retained all upstream layout and icon changes. Settings test conflict combined upstream alignment checks with this branch’s localization assertions. No script or simulation edits were made during integration.

Environment: macOS 26.6.2 (25G83) arm64, Apple LLVM 21, Python 3.14, Node 24.14.0, SDL3/SDL3_ttf, fast development build. Platform commands use `PATH=/Users/bradley/.nvm/versions/node/v24.14.0/bin:$PATH` because the default shell Node 20 cannot run the project’s TypeScript entry points. No runtime dependencies or simulation rules changed.

## Scope

The original pass added 252 English catalog entries (238 remain new against the integrated base, which independently added 14 overlapping settings keys) and filled 9,888 non-English entries across all 32 other supported languages. All 33 catalogs contain 3,316 keys. Three initial subagents divided translation and independently cross-reviewed language groups; parent handled source integration and additional review. A fresh second team reviewed all 10,197 new/filled values (309 per catalog) and made 246 targeted improvements across all 32 non-English languages: 50 Western, 125 Eastern/Central European, 71 Asian/RTL edits. English meaning was retained. See the three `polish-*.md` notes for concrete corrections and limits. Revised 116 tutorial messages and localized four mission titles. Existing authored/map-embedded tutorial script bytes and mission identifiers are preserved through a generated display alias table and full-source fingerprints.

Native scanner covers 1,585 files and 3,538 key references. Strict final JSON has no structural, missing or untranslated findings. Runtime-built names, arbitrary variable-assembled prose, custom content and new unregistered wrappers still require contextual review; this is not proof that every reachable sentence is translated.

## Checks

- `python3 data/check_translations.py --strict --json`: passed; `final-audit.json`.
- `python3 test/test_translations.py`: 10 passed; `final-python-tests.log`.
- `python3 test/test_font_coverage.py`: passed; `font-coverage.log`.
- From `platform/`, `npm run i18n:check`: 33 languages × 2,835 messages passed; browser generated artifacts current; `platform-i18n.log`.
- Focused platform source-contract, browser generation and runtime suites: 24 passed; `platform-tests.log`. Focused lint/format passed; `platform-lint.log`.
- Native build passed; 20 integrated engine cases passed (0 failed, 0 skipped). All 32 UILayout unit cases passed (0 failures, 0 skipped); results are recorded in `unit-layout-tests.log` and `unit-layout-junit.xml`. Results and all 33 language screenshots are in `native-tests.log`, `native-junit.xml`, and `native/`.
- Visually inspected English, Chinese, Arabic and Persian Controls previews. The refreshed Arabic/Persian previews preserve mixed Latin text and numbers (Mac, 50) and show joined text without raw localization keys. PNG previews are under `previews/`.
- `git diff --check`: passed.

Native test command: `python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'FontKerning/*' --filter 'TutorialLocalization/*' --filter 'SettingsLocalization/*' --filter 'Settings/*' --filter 'BuildingFamilyLinks/*' --filter 'BuildingLibrary/*' --jobs 2 --display-jobs 1 --timeout 240 --artifacts artifacts/translations/native --junit artifacts/translations/native-junit.xml`.

Unit layout command: `PYTHONPATH=$PWD python3 tools/dev_build.py unit-tests` followed by `python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary unit --filter 'UILayout/*' --jobs 2 --timeout 120 --artifacts artifacts/translations/unit-native --junit artifacts/translations/unit-layout-junit.xml`.

Native build command: `PYTHONPATH=$PWD python3 tools/dev_build.py engine-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2` (log: `final-native-build.log`). Native checks select FontKerning, TutorialLocalization, SettingsLocalization, the full software-rendered Settings interaction case, BuildingFamilyLinks and BuildingLibrary, using `python3 test/run_tests.py` with engine binary, two workers and one display worker. Independent read-only integration review confirmed all 32 non-English catalogs are byte-identical to the reviewed original commit (all 246 polish edits retained), the English AI profile matches master, and upstream Settings/Controls/UILayout code is preserved.

The final integrated run broadens coverage to every Settings and UILayout case because the upstream change affects shared controls. They verify all 973 aliases in every language, LF/CRLF source recognition, custom text isolation, UTF-8 wrapping, script/save continuity against a baseline, all-language Controls rendering and Arabic/Persian pixel order at multiple scales and after reload.

Old tutorial maps undergo existing coordinate healing on load. The save/load regression compares the localized path with an ordinary save/load baseline and separately verifies unchanged in-memory checksum, script bytes and queued orders at the display boundary.

## Hosted CI baseline failure

Run https://github.com/Globulation2/glob2/actions/runs/39629136545 failed during shared build-system contracts before check selection: `test_every_installed_music_set_is_owned_and_opus_only` expects 10 directories but the committed music tree contains 9. The test, `scons/web_assets.py`, and the entire `data/zik` tree are identical to integrated master (object IDs in `music-baseline-comparison.json`). The focused test reproduces the same failure locally (`music-baseline-reproduction.log`); full hosted log retained in `hosted-selection-failure.log`. No hosted engine/platform coverage is claimed. This pre-existing master regression is outside the translation changes and, under repository policy, does not block acceptance of the focused local evidence.

Executed-binary provenance confirms clean source revision `d1621b1c8485943ad75372303e918732584db4fb` for both test binaries (`provenance/build-provenance.json`, `unit-provenance/build-provenance.json`); binary SHA-256 hashes are in `binary-sha256.json`. All evidence files have hashes in `sha256.json`.

Author assessment: the focused local build, 20 engine cases, 32 unit layout cases, source/catalog/font checks and 24 platform cases adequately cover these presentation changes. The user explicitly requested commit, PR and merge. Hosted CI coverage is unavailable due to the unchanged master regression described above.

## Limits

Native build and execution were verified on macOS only. Windows, mobile and browser C++ binaries were not executed. Translation review was by AI agents, without a native-speaker certification. The 33-language Settings gallery checks text rendering and labels, not every app screen at every viewport. No release-readiness claim is made from the development build.
