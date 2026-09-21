# AI profile localization validation

Source commit: `e4ef8c4d7` in Globulation2/glob2.

This evidence branch is intentionally separate from the product branch.

`tests.log` records these macOS checks:

```
python3 test/test_translations.py
python3 test/test_font_coverage.py
python3 test/test_text_area_layout.py
python3 data/check_translations.py --strict
```

`build.log` records the final `scons release=1 server=0 custom-setup-test -j4` build. The client also built successfully with `scons release=1 server=0 custom-setup-test build/src/glob2 -j4`.

The PNGs are actual CustomGameSetupHarness captures (BMP converted to PNG): English at 1000x700, Chinese and Arabic at 640x480. Capture with an isolated GLOB2_USER_DIR and `./build/src/CustomGameSetupHarness OUTPUT profiles LANGUAGE`, or `profiles-large` for the larger size. Language codes here are en, zh-cn and ar.

Visual checks were representative, not exhaustive across every locale. Translations have not received independent native-speaker review. No simulation behavior changed; cross-platform simulation checksums were not run.
