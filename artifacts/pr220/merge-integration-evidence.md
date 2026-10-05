# Pre-merge integration verification

PR head: a2dfe14031c45f1725839993129abc1199d7ceea
Current master: eacc18128854144e7d3f48770d3c53eb525917e4
Combined Git tree: 81de79d754fa38549c4b02cf5d38d1d99c94f8fc

Master added editor unit sprite selection changes (#807). A clean automatic merge was staged without committing, then rebuilt and tested. macOS 26.6.2 arm64, Apple clang 21, pinned dependencies and build flags match the original evidence README. The provenance records the temporary staged merge as dirty; the combined tree above identifies the exact tested source.

Commands:

```sh
git merge --no-commit --no-ff origin/master
GLOB2_SDL3_PREFIX=build/sdl3/prefix scons -j8 release=1 server=0 tests build/darwin/client/release/src/glob2
python3 test/run_tests.py --filter 'ImageAssets/*' --filter 'SpriteLoad/*' --filter 'SpriteSheets/*' --filter 'AssetLoader/*' --filter 'UnitHighResolutionCache/*' --filter 'HighResolutionIntegration/*' --filter 'TerrainRuntime/*' --display-jobs 1 -j4 --junit artifacts/pr220/merge-integration-tests.xml --artifacts artifacts/pr220/merge-integration-render --write-inventory artifacts/pr220/merge-integration-inventory.json
```

Release build passed. All 18 grouped jobs / 37 native cases passed, with no failures or skips. Logs, JUnit report, inventory, provenance and rendering captures accompany this record. The temporary merge was then aborted to preserve the reviewed PR head. Original pipeline validation and platform/playtesting limits remain in evidence/README.md.
