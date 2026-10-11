Local / VM verification for PR #1119

- Tested commit SHA: 44c275ae3088f54e586613e4dc7d3d278e001ada
- Base revision and integration state: 1e113da18493b8dc626edc7744fd9118ab2d651f; PR head includes a merge with this base, clean working tree.
- Environment: Ubuntu 26.04.1 LTS, Linux x86_64, GCC/G++ 15.2.0; display tests through Xvfb.
- Dependencies/build: repository-pinned SDL 3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2, SDL_net 3.2.0 and WebP 1.6.0, built using scons/sdl3_dependencies.py; other native dependencies from this host. Full-featured fast development build, dev_fast=1, -O0 -g1, default eight jobs, linker=auto, no PCH/unity. Build log records flags and library paths. This is not release/performance evidence.
- Coverage: newer ticks remain eligible after camera requests are consumed; stale retained-camera work leaves capacity for fresh worlds; native mailbox and serial producer; paused updates without recapture; snapshot immutability and lifetimes; pacing and menu publication; shared renderer contracts added on the new base; 128 per-tick checksums compared at render ceilings 25, 60, 120 and Unlimited.
- Results: build exit 0; 32 engine cases passed; 23 renderer unit cases passed; 3 scene-boundary and 16 shader tests passed; all commands exit 0. A separate pre-integration negative control disables both scheduler guards and fails the new regression with retained tick 0 versus live tick 1. Fixed sources were restored before integration validation.
- Omitted/limits: Windows, mobile, browser runtime/builds, ThreadSanitizer, manual camera playtesting and release/performance testing are unverified. Save/load and replay/network acceptance suites were not repeated because this change affects only presentation admission, without changing authoritative state, rules, serialized bytes or version gates. Checksum coverage is local Linux coverage, not cross-platform equivalence.
- Maintainer acceptance: focused author verification is accepted for this presentation-only revision under AGENTS.md; merging was explicitly requested in the chat.

Exact commands:

```sh
python3 scons/sdl3_dependencies.py --prefix /tmp/glob2-pan-sdl --work /tmp/glob2-pan-sdl-work --jobs 8
GLOB2_SDL3_PREFIX=/tmp/glob2-pan-sdl python3 tools/dev_build.py engine-tests unit-tests
GLOB2_SNAPSHOT_VERIFY=1 python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'SceneExtract/*' --filter 'EngineSession/camera refreshes*' --filter 'EngineSession/new client requests*' --filter 'EngineSession/native presentation fallback*' --filter 'EngineSession/paced sessions*' --filter 'EngineSession/menu colony rendering*' --filter 'EngineSession/render ceilings preserve*' --junit artifacts/camera-animation/integration/engine-tests.xml --artifacts artifacts/camera-animation/integration/engine-tests --verbose
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary unit --filter 'RenderDevice/*' --filter 'RendererSelection/*' --junit artifacts/camera-animation/integration/renderer-tests.xml --artifacts artifacts/camera-animation/integration/renderer-tests --verbose
python3 test/build_system/test_scene_boundary.py
python3 test/build_system/test_renderer_shaders.py
```

The archive contains integration build/test logs, JUnit reports, per-tick checksum files and scene-test screenshots. The old-behavior log/report is from the original pre-integration base c6a7eae787a0f945619a622ed99ac755c2632689 and is a negative control, not final-revision validation.

Archive SHA256: 1fd895d177b95daecf424ed45ca6ae5f983b08728665c91770a6f2f91b94144f
