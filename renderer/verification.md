# Artwork lifecycle regression verification

Tested commit: 0f611fbe9de369c50d424b9389869f03a1e3fbd4. Integration base: 5443f425e0ff00065457ce8baa68238557db49f6. Clean source; master fetched again before final checks with no newer changes.

macOS 26.6.2, Darwin 25.6.0 arm64; Apple LLVM 21.0.0 (clang-2100.3.34.2); development dependency prefix build-18986503a53e176afb4191c6. Native dev_fast=1, -O0 -g1, linker=auto; PCH/unity off. This development build is test evidence and is not shipped. Official deployment builds release artifacts independently.

Commands:

```sh
python3 tools/dev_build.py engine-tests unit-tests -j4
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'HighResolutionIntegration/*' --filter 'UnitHighResolutionCache/*' -j 1 --junit artifacts/building-studio/depot-e2e/artwork-engine-tests.xml --artifacts artifacts/building-studio/depot-e2e/native-renderer
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'SpriteSheets/*' --filter 'HighResolutionIntegration/*OpenGL*' -j 1 --junit artifacts/building-studio/depot-e2e/artwork-final-tests.xml --artifacts artifacts/building-studio/depot-e2e/native-renderer-final
cd platform
npm ci
npm run typecheck
GLOB2_DESIGN_SYSTEM_SHA=acd277710acfebf528e1fc6f653d55fdf75de4b2 npm exec --workspace @glob2/web -- vite build
```

Node 24.19.0. Platform install, all three TypeScript checks and production Vite bundle passed. Shared design-system revision acd277710acfebf528e1fc6f653d55fdf75de4b2. Docs checker: 307 documents, 0 errors, 74 links.

Native build passed. First run passed software integration and both unit-artwork cache cases; OpenGL reached the harness 120-second artwork-completion deadline while concurrent builds were active. Retained its failure log. After this task's compilation finished, the focused OpenGL rerun passed in 189.9 seconds and all seven SpriteSheets cases passed. Eleven distinct focused cases pass in total. New screen-lifetime assertions, explicit reloads, alpha integrity, cache fallbacks, zoom, replay drawing, stable simulation checksums and explicit optional-layer release are covered.

Coverage limits: no Windows/Linux native rendering or other browser engines; fullscreen opt-in omitted. Simulation, save/replay/network formats and APIs are unchanged. Live browser baseline crash and exported Depot save are under ../depot/. Post-deployment repeated browser load/exit is a separate required check, not claimed here.
