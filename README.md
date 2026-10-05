# Software-only terrain test verification

Tested head6775c8c01 (fullheadinPRcomment), base79c8d65f52cfea0a91efa3c104f97d79fd5de54d. Refetched master19b15ec35 after validation; only32translationcatalogs changed, no overlap with test/compiler configuration or terrain drawing logic. No rebase needed.
Ubuntu26.04.1x86_64, GCC15.2.0, Python3.14.4/SCons, pinned SDL3.4.16/SDL3_image3.4.6/SDL3_ttf3.2.2 SDK. Asset encoder uses repository-pinned Python/Pillow/WebP. Existing recording libraries reused only after build helper validates their manifest; recording source/flags/toolchain unchanged. New separate software build output; all engine source linked without OpenGL or epoxy libraries.

```
GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix GLOB2_RECORDING_PREFIX=build/linux/client/release/recording/prefix GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python scons -j12 release=1 server=0 opengl=0 --build=build-software-terrain build-software-terrain/src/glob2 engine-tests
python3 test/run_tests.py --build-dir build-software-terrain --binary engine --filter 'TerrainPresentation/*' --junit artifacts/terrain-software-tests/junit.xml --artifacts artifacts/terrain-software-tests --verbose
```

Both client and full engine harness link (exit0). Five terrain cases pass,0failed/0skipped,1.9seconds: software/cache pixel equivalence, all16decorativeedge masks, metadata animation/backgrounds, image import legacy gameplay edges, exported colors. Pixel artifacts attached.

Compiled the exact test translation unit in both configurations with the same actual SCons compile command (GL enabled by generated HAVE_OPENGL definition; command attached). Original unguarded software object references epoxy_glFinish/GetIntegerv/ReadPixels; final software object has no epoxy symbols. Final GL object retains those symbols and the OpenGL test registration. The shader/drawing/readback bodies are unchanged.

Only test configuration guards/header inclusion change; no production/simulation/save/replay/network changes or SIM_REVISION bump. No local full GL harness execution or GCC11/13 compilation; original hosted failures on both compilers retained below, and focused local GCC15softwarelink verifies the missing-symbol repair. GL translation-unit compilation checks retained code; it is not claimed as GPU runtime verification. An early local edit used the wrong generated config header path and failed compile; corrected before the final tested head, whose complete build log is attached.

Original failures:
https://github.com/Globulation2/glob2/actions/runs/37281174773/job/111671365101
https://github.com/Globulation2/glob2/actions/runs/37281174773/job/111671365209
Maintainer acceptance: Codex accepts focused verification for this test-only build repair under AGENTS.md.
