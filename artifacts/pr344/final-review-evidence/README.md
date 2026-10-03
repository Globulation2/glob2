# PR 344 final local validation

This evidence matches feature revision 9b47ae493, rebased onto master 1ba640b52, including rendering, recording, sprite sheets, Hive and frame-pacing integration. See `manifest.json` for binary hashes and compiler/dependency inputs. Development evidence is retained on this separate branch rather than in maintained feature documentation.

All 519 engine cases passed, including the full slow generator catalogue. The full 617-case unit suite passed on an isolated rerun. The first combined native run has one retained performance failure: the unchanged standalone 512-tile fertility benchmark measured 102,772 CPU microseconds against its 100,000 limit. The full unit rerun measured 92,127; an additional focused rerun measured 87,479. The limit was not changed. Original and retry reports/logs are included, so the initial report is not presented as an uninterrupted green run.

All 27 selected software/portable/OpenGL rendering cases passed. These exercise diagnostic output and exception restoration, repeated graphics lifetimes, bars, resize, high-resolution artwork and wrapped seams. The desktop sidebar fixture explicitly scopes desktop presentation. Linux display checks used isolated Xvfb displays, Openbox and Mesa software OpenGL.

All 21 production diagnostics CLI contracts and 50 map CLI contracts passed. Captures disabled, fields, PNG and threaded PNG have identical per-tick checksums and decompressed final saves. Continuation and capture write failures preserve simulation execution. Command records, paired saves, traces, summaries and one complete field/PNG capture are included. Standalone and overlay PNGs plus the 512-tile map capped to 128 pixels are included; the final render used 84,464 KiB peak process RSS.

The build-system contracts passed 286 tests, three skipped. Simulation-version consistency and whitespace checks passed. No simulation revision, save format or AI policy change is introduced by this PR.

Reproduce with a native release build:

```sh
scons -j12 release=1 build/linux/client/release/src/glob2 engine-tests unit-tests
python3 test/run_tests.py --no-display -j6 --timeout 1800
python3 test/run_tests.py --binary unit --no-display -j1 --timeout 1800
python3 test/run_tests.py --binary unit --filter '*corridor protection and fertility caches*' --no-display
python3 test/test_game_diagnostics.py build/linux/client/release/src/glob2
python3 test/test_map_cli.py build/linux/client/release/src/glob2
"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s tests/build_system
```

Rendering selections are listed in the rendering log/report. The maintained CLI and telemetry guides describe the new commands. Hosted CI was waived by the maintainer conditional on passing relevant local checks. GCC 13, Windows, macOS and WebAssembly were not locally verified; these results do not establish cross-platform checksum equivalence.
