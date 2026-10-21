# Translation validation

PR: https://github.com/Globulation2/glob2/pull/1243

Source revision: `04fa4544ed19a97e09d4c33cc5218824e87a9d88` (clean committed working tree). Fetched master: `f0104aab54ce22e6bbb7311c12924d75ad60d83a`; upstream changes did not require integration edits. `git merge-tree --write-tree HEAD origin/master` confirmed a clean merge; unrelated AI changes were not rebased into this branch.

Environment: macOS 26.6.2 (25G83) arm64, Apple LLVM 21, Python 3.14, Node 24.14.0, SDL3/SDL3_ttf, fast development build. Platform commands use `PATH=/Users/bradley/.nvm/versions/node/v24.14.0/bin:$PATH` because the default shell Node 20 cannot run the project’s TypeScript entry points. No runtime dependencies or simulation rules changed.

## Scope

Added 252 English catalog entries and filled 9,888 non-English entries across all 32 other supported languages. All 33 catalogs contain 3,316 keys. Three initial subagents divided translation and independently cross-reviewed language groups; parent handled source integration and additional review. A fresh second team reviewed all 10,197 new/filled values (309 per catalog) and made 246 targeted improvements across all 32 non-English languages: 50 Western, 125 Eastern/Central European, 71 Asian/RTL edits. English meaning was retained. See the three `polish-*.md` notes for concrete corrections and limits. Revised 116 tutorial messages and localized four mission titles. Existing authored/map-embedded tutorial script bytes and mission identifiers are preserved through a generated display alias table and full-source fingerprints.

Native scanner covers 1,585 files and 3,539 key references. Strict final JSON has no structural, missing or untranslated findings. Runtime-built names, arbitrary variable-assembled prose, custom content and new unregistered wrappers still require contextual review; this is not proof that every reachable sentence is translated.

## Checks

- `python3 data/check_translations.py --strict --json`: passed; `final-audit.json`.
- `python3 test/test_translations.py`: 10 passed; `final-python-tests.log`.
- `python3 test/test_font_coverage.py`: passed; `font-coverage.log`.
- From `platform/`, `npm run i18n:check`: 33 languages × 2,835 messages passed; browser generated artifacts current; `platform-i18n.log`.
- Focused platform source-contract, browser generation and runtime suites: 24 passed; `platform-tests.log`. Focused lint/format passed; `platform-lint.log`.
- Native build passed; all 12 selected native cases passed (0 failed, 0 skipped). Results and all 33 language screenshots are in `native-tests.log`, `native-junit.xml`, and `native/`.
- Visually inspected English, Chinese, Arabic and Persian Controls previews. The refreshed Arabic/Persian previews preserve mixed Latin text and numbers (Mac, 50) and show joined text without raw localization keys. PNG previews are under `previews/`.
- `git diff --check`: passed.

Native test command: `python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'FontKerning/*' --filter 'TutorialLocalization/*' --filter 'SettingsLocalization/*' --filter 'Settings/*software rendering*' --filter 'BuildingFamilyLinks/*' --filter 'BuildingLibrary/*' --jobs 2 --display-jobs 1 --timeout 240 --artifacts artifacts/translations/native --junit artifacts/translations/native-junit.xml`.

Native build command: `PYTHONPATH=$PWD python3 tools/dev_build.py engine-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2` (log: `final-native-build.log`). Native checks select FontKerning, TutorialLocalization, SettingsLocalization, the full software-rendered Settings interaction case, BuildingFamilyLinks and BuildingLibrary, using `python3 test/run_tests.py` with engine binary, two workers and one display worker. They verify all 973 aliases in every language, LF/CRLF source recognition, custom text isolation, UTF-8 wrapping, script/save continuity against a baseline, all-language Controls rendering and Arabic/Persian pixel order at multiple scales and after reload.

An initial test used the wrong reader for a compressed map; that fixture was corrected. The initial checksum oracle also assumed loading an old map performed no legacy repairs; the final test compares the localized path with the ordinary save/load baseline. Earlier failure logs are retained as evidence, not final results.

## Limits

Native build and execution were verified on macOS only. Windows, mobile and browser C++ binaries were not executed. Translation review was by AI agents, without a native-speaker certification. The 33-language Settings gallery checks text rendering and labels, not every app screen at every viewport. No release-readiness claim is made from the development build.
