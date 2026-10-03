# PR 344 local validation evidence

See `manifest.json` for source revisions, compiler, flags, dependencies and binary hashes. This branch retains development evidence separately from maintained feature documentation.

The full Linux native run at revision 1488d8703 passed 515 jobs, zero failures, with 1,214 reported cases. After integrating master 3092bdeda (Hive commander, Map Studio and sprite-index compatibility), the refreshed non-slow native run at feature revision 42494d5fb passed 441 jobs, zero failures, with 1,138 reported cases. Unchanged slow generator contracts are covered by the full report; this later run refreshes the changed engine, Scene and test boundaries. Reports and selection inventories are included.

`latest-cli` contains the latest production command record, initial and final saves, per-tick checksum traces, continuation evidence, one complete field/PNG capture, standalone overlays and the 512-tile map rendered to 128 pixels. All 21 diagnostics CLI contracts and 50 map CLI contracts passed. Off, fields, PNG and threaded PNG traces and decompressed final saves match exactly. Capture output failures preserve simulation execution. Final large-map render peak process RSS was 83,348 KiB.

The build-system contracts passed 286 tests, three skipped. The separate rendering report covers software, portable and OpenGL paths, offscreen state restoration, resource lifetimes, resized views, high-resolution artwork, bars and wrapped seams. The sidebar fixture explicitly scopes desktop presentation because it exercises desktop coordinates.

Reproduce using a native release build:

```sh
scons -j12 release=1 build/linux/client/release/src/glob2 engine-tests unit-tests
python3 test/run_tests.py --no-display -j4 --timeout 1800
python3 test/run_tests.py --quick --no-display -j4 --timeout 1800
python3 test/test_game_diagnostics.py build/linux/client/release/src/glob2
python3 test/test_map_cli.py build/linux/client/release/src/glob2
"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s tests/build_system
```

Linux display checks used isolated Xvfb displays with Openbox and Mesa software OpenGL. Rendering selections and durations are recorded in the report and log. Hosted CI was waived by the maintainer, conditional on relevant local checks passing. GCC 13, Windows, macOS and WebAssembly were not locally verified. These observations do not establish cross-platform checksum equivalence.
