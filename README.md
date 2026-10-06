# HD sprite batch assertion repair verification

Tested commit d667ecc94ec88aa3fc5dc071905345b5d6b10611 against fetched master 2ed5ca5a3ab210612b7b435fe51b86bc5c7fa21e on Ubuntu 26.04.1 x86_64, GCC 15.2, Clang 18.1.8, SDL 3.4.16/image 3.4.6, software Mesa under Xvfb. Actual compiler commands and linked objects are attached. Changed test and provenance rebuilt; existing compatible library objects reused. No full asset exporter or full engine matrix claim.

The original binary reproduced the hosted failure with the complete HD resource atlas: exact draw count 1 versus obsolete 3. With a standalone partial HD pack the original test passed. Final revision passed 4 resource batch cases with the atlas and 6 resource/sprite-sheet cases with standalone resources, including pixel equality at five zooms, 10,000-quad limit, portable rendering, exception recovery and independently encoded lossy atlas alpha validation. Clang compiled the changed fixture. No tests skipped. Build provenance, JUnit, logs and input hashes attached.

Commands (from repository root):

```sh
GLOB2_SDL3_PREFIX=$PWD/build/sdl3-ci/prefix scons -Q -j4 release=1 build/linux/client/release/test/unit-libgag_src_SpriteDrawBatchTest.o build/linux/client/release/test/unit-support_TestMain.o
```

Then run the exact archive/link commands in unit-link-commands.json. This avoids the unrelated asset exporter; final-link.log is empty because all commands succeeded without output.

```sh
LD_LIBRARY_PATH=$PWD/build/sdl3-ci/prefix/lib GLOB2_ASSET_DIR=$PWD/artifacts/terrain-ci-repair/runtime-delegated GLOB2_EXPERIMENT_TEXTURE_DIR=$PWD/build/linux/client/release/runtime-assets-staging-ea6blwqr/data/highres/v1 python3 test/run_tests.py --binary unit --build-dir build/linux/client/release --quick -j2 --filter '*resource sprite batch*' --artifacts artifacts/hd-sprite-batch-repair/atlas-final --junit artifacts/hd-sprite-batch-repair/atlas-final.xml
LD_LIBRARY_PATH=$PWD/build/sdl3-ci/prefix/lib GLOB2_ASSET_DIR=$PWD/artifacts/terrain-ci-repair/runtime-delegated python3 test/run_tests.py --binary unit --build-dir build/linux/client/release --quick -j2 --filter '*resource sprite batch*' --filter '*lossy*atlas*' --filter '*portable*HD*' --artifacts artifacts/hd-sprite-batch-repair/standalone-final --junit artifacts/hd-sprite-batch-repair/standalone-final.xml
```

Atlas inputs are existing outputs of the pinned runtime exporter from current artwork; standalone inputs are the earlier exporter validation pack. asset-inputs.json records every resource and index hash in both packs. Generate shipped runtime assets with `python3 tools/package_assets.py --source . --output <pack> --platform linux` for equivalent atlas coverage. Existing SpriteSheets tests create their own controlled fixtures. Raw hosted failures remain accessible in run 37396129432, GCC 11 job 112061465142 and GCC 13 job 112061623099; retained locally unchanged too.

Coverage rationale: only a test expectation changes; no production renderer, pixels, simulation, save/replay/network or asset data changes. Existing exact pixel checks preserved, exact submission count remains 1 for a shared atlas and 3 for standalone HD/portable sources. Omitted Windows/macOS/browser runtime and full engine suite because test-only branch selection was covered locally; ongoing full master CI and subsequent retained push confirm hosted integration separately. No simulation version bump needed.
