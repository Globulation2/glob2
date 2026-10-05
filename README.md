# Render FPS translation repair verification

PR761 tested head66e1c46757b54e13337b5705c5eeb7b31e776880, base/master3607e90980ad42766b6d5663ac41e1a2b93e0f93 (fetched before final validation). Linux x86_64 Python3.14.4, pinned SDL3.4.16/SDL3_ttf3.2.2. Only 96 missing localized values across32catalogs added; existing values preserved byte-for-byte. No engine/simulation/format change.

Commands: python3 test/test_translations.py (5pass); python3 data/check_translations.py --strict (0structuralerrors,0missingkeys in everycatalog, existing pending translations unchanged); GLOB2_SDL3_PREFIX=<pinned-native-prefix> LD_LIBRARY_PATH=<pinned-native-prefix>/lib python3 test/test_font_coverage.py (1pass); git diff --check (pass). First font invocation lacked SDL3_ttf discovery; configuring the documented prefix passed. Initial edit trimmed a final intentionally empty value; corrected before commit, all catalogs now structurally valid.

Coverage targets catalog identities, placeholders, non-English values and all bundled glyphs. Native/browser UI visual checks and native-speaker review of phrasing are not claimed. No game pacing changes; translated help says render work is reduced while game speed stays unchanged. Hosted translation failure evidence: https://github.com/Globulation2/glob2/actions/runs/37261048282/job/111617332567 (96 missing values).
