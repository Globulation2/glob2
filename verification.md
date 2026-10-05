Local / VM verification

- Tested commit: `2071eea124e51054d4a1ee5065fc75bac7988e0d`; clean working tree.
- Base: `857b69530254983efe1659b85467a17fa1973dfb` (master render-FPS integration included and reviewed).
- Environment: Linux x86_64, Ubuntu GCC 15.2.0, C++20, optimized release build; shared 32-logical-CPU development host. Xvfb/X11 and Mesa software OpenGL for rendering checks. Node 22 for TypeScript/platform checks; Node 24 for Playwright's TypeScript test server.
- Dependencies: pinned native SDL3 3.4.16, SDL3_image 3.4.6, SDL3_ttf 3.2.2, SDL3_net 3.2.0 and WebP 1.6.0, including current master SDL PNG patch. Complete dependency identity and build configuration are in `review-metadata.json`; exact compiler/link commands are in build logs.
- Review: author plus three subagents; two review rounds and additional shared-header/FPS integration follow-up. Fixed ownership/cancellation, variable-atlas admission, weak cache cleanup, polling deadlines, private HD staging, per-subscriber authorization, content-addressed image URLs and export media types. Documentation updated alongside behavior.
- Coverage: native worker scheduling/read backpressure, shared-pixel ownership/alpha, sprite sheets, full unit suite, actual software/OpenGL rendering, HD replacement, online skin authorization/refresh/cancellation, map previews, fonts, UI icons, music, current FPS integration and native startup. Platform tests cover persisted immutable renditions, replica winner selection, signatures/routes, migrations, GC and exact exported draft bytes. Browser flows cover map preview interaction and skin draft restore/publish/moderation/fallback.

Commands and results (exit 0):

```sh
GLOB2_SDL3_PREFIX="$PWD/artifacts/asset-loader/native-prefix" scons -j8 release=1 unit-tests engine-tests asset-loading-benchmark skin-preview skin-game-preview online-screens-probe build/linux/client/release/src/glob2
GLOB2_ASSET_THREADS=4 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 python3 test/run_tests.py --binary unit -j4 --display-jobs 1 --junit artifacts/asset-loader/review-unit-final.xml --artifacts artifacts/asset-loader/review-unit-final
GLOB2_ASSET_THREADS=4 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 python3 test/run_tests.py --binary engine -j2 --display-jobs 1 --filter '*HighResolution*' --filter '*Skin*' --filter '*Preview*' --filter '*Font*' --filter 'UIIcons/*' --filter '*Music*' --filter 'OnlineResources/*' --filter 'ScreenExecution/*' --filter 'SettingsGraphics/*' --filter 'EngineSession/render ceilings*' --junit artifacts/asset-loader/review-engine-final.xml --artifacts artifacts/asset-loader/review-engine-final
python3 artifacts/asset-loader/build-tsan.py
TSAN_OPTIONS=halt_on_error=1 GLOB2_ASSET_THREADS=4 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy artifacts/asset-loader/glob2-assets-tsan --test-suite=AssetLoader
xvfb-run -a -s '-screen 0 1024x768x24 -noreset' python3 artifacts/asset-loader/startup-smoke.py
node --test browser/unit/asset-loader.test.js
python3 -m unittest discover -s test/build_system -p test_ci_policy.py
npx --prefix platform vitest run --root platform apps/api/test/skinPublishing.test.ts apps/api/test/skinDrafts.test.ts apps/api/test/skinModeration.test.ts apps/api/test/skins.test.ts apps/api/test/catalog.test.ts apps/api/test/studio.test.ts apps/api/test/rooms.test.ts apps/api/test/webpRendition.test.ts packages/db/test/schema.test.ts apps/worker/test/reliability.test.ts packages/protocol/test
(cd platform && npx vitest run apps/api/test/accountExport.test.ts && npm run typecheck && npm run build --workspace @glob2/web)
(cd platform/apps/web && npx --yes --package=node@24 -c 'node ../../node_modules/@playwright/test/cli.js test -c e2e/playwright.config.ts map-preview.spec.ts skins.spec.ts --grep "map preview|touch panning|rectangular maps|account drafts|publishes immutable|reports match paint|degrades safely"')
```

- Native unit: 776 cases, no failures. Focused engine: 33 cases, no failures.
- TSan: shared loader and its test objects instrumented; 10 cases/605 assertions pass, no reported races. Remaining game/third-party objects are not instrumented.
- Native interactive OpenGL startup and clean close pass; screenshot included and visually inspected.
- Platform/protocol: 86 tests across 13 files; account export: 7 additional tests. Typecheck and web build pass. Focused changed-file ESLint/Prettier pass.
- Browser loader: 13 tests pass. Platform browser flows: 13 pass, 1 intentional desktop touch skip. CI policy: 19 tests pass.
- Emscripten serial and pthread syntax checks pass for AssetLoader.cpp, SpriteLoad.cpp, Sprite.cpp and DrawableSurface.cpp, using C++20/fwasm-exceptions, WebGL2/OpenGL defines, current build config/native headers; pthread variant adds `-pthread`. Logs included. This does not establish a complete browser game build/runtime or context-loss behavior.
- Per-tick checksums at unlimited/25/60/120 render FPS are identical: SHA-256 `8df73b1913668abc4dab977f951fab0a5e1c36edeb5b2dc1398e321cefd966a4`; all four traces included.

Benchmark failed-job counts include optional missing `.sheet` probes; every required family reaches readiness and is fingerprinted.

Performance: repeated warm-cache tests of this same revision's cooperative serial mode versus default worker settings, not a comparison against previous master. 91 families/2,538 frames. Ten randomized samples per CPU/IO configuration (80 runs): serial median363.68ms, default8CPU/2IO median93.34ms; CPU-only median RSS68.0 versus88.8MiB. Ten samples per software OpenGL configuration (20 runs), including glFinish/error checks: serial median540.0ms versus default222.35ms. All100 base-layer fingerprints match `7adfda3471c05257`. Scripts, full per-run JSON and timing ranges included. No filesystem cache eviction; shared host; no cold-disk or hardware-GPU performance claim.

Omissions: full engine inventory, Windows/macOS/mobile execution, complete serial/threaded WebAssembly game runtime and runtime context-loss coverage, whole-game sanitizer instrumentation, cold-cache I/O and dedicated hardware-GPU benchmarks. This visual/protocol change leaves simulation rules, save/replay formats and SIM_REVISION unchanged; the focused suite includes saved replay-backed rendering and matching native simulation traces. Cross-platform simulation equivalence has not been established by these local checks.

Evidence: archive contains successful final build/test logs, JUnit, metadata, benchmark scripts/results, screenshots, save/replay fixtures and checksum traces. Development evidence lives on a dedicated evidence branch; no generated evidence was committed to the feature branch.

Maintainer acceptance: merge is explicitly authorized by the user; the author accepts this focused local evidence with the above limitations after three independent subagent reviews and two iterations. Hosted PR checks are cheap contracts only; they are not engine verification. Full master CI remains asynchronous after merge.
