# Contributor documentation disposition

| Original | Destination | Disposition |
| --- | --- | --- |
| docs/development/reference.md:11–176 | docs/architecture/ai-observations.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:449–671 | docs/development/package-size.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:672–852 | docs/development/rendering-benchmarks.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:853–958 | docs/architecture/rendering.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:959–1407 | docs/architecture/simulation.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:1408–1561 | docs/architecture/resource-growth.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:1562–1718 | docs/development/gradient-benchmarks.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:1719–1739 | docs/development/conventions.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:1740–1816 | docs/development/development-storage.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:2303–2480 | docs/development/verification.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:2481–2539 | docs/development/untrusted-inputs.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:2540–2584 | docs/development/memory-benchmarks.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:2585–2655 | docs/architecture/persistence.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:2656–2743 | docs/development/media-dependencies.md | Split and edit into a single-purpose guide |
| docs/development/reference.md:2744–2809 | docs/development/build-options.md | Split and edit into a single-purpose guide |
| docs/development/javascript.md | docs/scripting/javascript.md | Move; correct new-game order-delay default from GameHeader and remove stale current format/protocol claims |
| docs/development/javascript-api.md | docs/scripting/javascript-api.md | Move; correct new-game order-delay default from GameHeader and remove stale current format/protocol claims |
| test/README.md (tooling) | docs/development/testing/tooling.md | Group retained regression scenarios by subsystem |
| test/README.md (network) | docs/development/testing/network.md | Group retained regression scenarios by subsystem |
| test/README.md (simulation) | docs/development/testing/simulation.md | Group retained regression scenarios by subsystem |
| test/README.md (ai) | docs/development/testing/ai.md | Group retained regression scenarios by subsystem |
| test/README.md (rendering) | docs/development/testing/rendering.md | Group retained regression scenarios by subsystem |
| test/README.md (maps) | docs/development/testing/maps.md | Group retained regression scenarios by subsystem |
| test/README.md (platforms) | docs/development/testing/platforms.md | Group retained regression scenarios by subsystem |
| test/README.md (telemetry) | docs/development/testing/telemetry.md | Group retained regression scenarios by subsystem |
| test/README.md (scripting) | docs/development/testing/scripting.md | Group retained regression scenarios by subsystem |
| test/README.md (ci) | docs/development/testing/ci.md | Group retained regression scenarios by subsystem |
| test/README.md (assets) | docs/development/testing/assets.md | Group retained regression scenarios by subsystem |

## Source review

This pass checks documented ownership, command paths, constants and tests against current source. It does not claim execution or cross-platform qualification.

| Retained topic | Sources inspected | Result / disposition |
| --- | --- | --- |
| Build commands, fast options and storage | tools/dev_build.py; tools/dev_environment.py; SConstruct; scons/shared_dependencies.py | Replaced mixed onboarding section with build/run guide; kept current explicit options and cache contracts; release runbooks delegated to releases owner. |
| Dependency and asset preparation | scons/sdl3-versions.json; scons/sdl3_dependencies.py; scons/mac_image_dependency.py; scons/native_image_dependency.py; tools/package_assets.py | Corrected stale SDL_image 2.8.12 claims to SDL3_image 3.4.6; removed nonexistent browser/ports claim; retained exporter and cache contracts. |
| AI observation and worker ownership | src/engine/sim/snapshot/BufferPool.h; SnapshotStore.h; src/common/ComputeExecutor.h; src/game/GameHeader.h/.cpp | Pool bound 22 and match AI delay 8 confirmed; retained immutable publication and fixed logical deadlines. |
| Simulation and ecology | src/game/Game_sync.cpp; src/map/MapState.h; MapStateView.h; ResourceGrowth.h/.cpp; src/map/gradient/MapGradientScheduling.cpp | Grouped state, RNG, terrain, capacity and field ownership; removed duplicated contributor policy. Current stream versions remain documented as version transitions. |
| Scene, raster, fog and zoom | src/engine/EngineRun.cpp; src/render/FogFade.h; src/engine/sim/SimulationRunner.h; libgag/src/SurfaceRaster.cpp; src/render/scene/ | Split rendering, software backend and adaptive zoom; fixed abbreviated SimulationRunner path. Benchmark commands live in development guides. |
| Serialization and save lifecycle | libgag/include/DeferredStream.h; libgag/src/FileManagerGzip.cpp; src/app/Version.h; src/replay/ReplayReader.h | Retained historical format transitions with durable loading floor; removed uncited 1.93/3.96 GiB result and unrelated cloud-overlay investigation. |
| Headless CLI and AI dispatch | src/app/GlobalContainerArgs.cpp; src/app/cli/Headless.cpp; src/engine/EngineRun.cpp; src/ai/AI.cpp | Corrected moved source entrypoints; removed unsupported relative AI-strength claim. |
| Native tests and domain runbooks | test/tests.py; test/SConscript; test/run_tests.py; test/support/Glob2Test.h; test/support/EngineFixtures.h; referenced check_*.py and test_*.py scripts | Registry and isolation remain canonical; >60 retained runbooks grouped by subsystem. Removed time-bound one-release alias promise and prototype label for shipping executor. |
| Performance and network telemetry | src/net/turn/TurnTelemetry.h; platform/packages/protocol/src/network.ts; tools/memory_benchmark.py; tools/gradient_benchmark.py; tools/test_gradient_benchmark.py | Corrected protocol source path; schemas separated from pipeline campaigns; replaced unlinked 1% overhead result with repeatable measurement guidance. |
| Browser implementation and audio | browser/toolchain.json; scons/web_build.py; browser/music-worker.js; browser/music-output.js; src/audio/MusicBuffer.h | Pinned SDK verified; 24/20/48 block audio bounds confirmed; audio extracted; CI prose replaced with canonical selection policy link. |
| Browser decisions, viewport, storage and transport | browser/ApplicationHost.cpp; browser/canvas-size.js; browser/Audio.cpp; browser/audio.js; browser/storage.js; libusl/ | Accepted status normalized; uncited sanitizer success replaced with qualification instruction; no undocumented release-readiness claim retained. |
| JavaScript authoring and API | src/game/GameHeader.h/.cpp; src/scripting/javascript/; examples/javascript/glob2.d.ts | Both default-zero AI delay claims corrected to eight; API and authoring moved together into scripting. Contracts remain linked to declarations. |
| Declarative UI | libgag/include/ui/; src/ui/; src/engine/EngineRun.cpp | Retained screen tree, model invalidation, focus and host contracts; updated links to focused presentation docs. |
| Verification and CI | .github/workflows/build.yml; .github/scripts/ci_policy.py; test/ci-compatibility.json; AGENTS.md | Retained exact labels, nightly schedule and source/evidence identity contracts; did not claim any hosted runs performed for this edit. |
| Historical architecture | docs/development/legacy-architecture.txt | Deleted per user-selected prune policy; current overview replaces stale architecture. Git history preserves historical context. |

## Deleted material

- Original reference.md: all durable sections moved to focused pages; duplicate build/release/CI introductions replaced by canonical guides. Windows Store runbook is retained by the releases owner.
- Historical cloud-overlay investigation in save continuation: unrelated experiment outcome.
- Fixture-specific measured memory reduction and undocumented performance overhead comparison: transient evidence without a reviewable retained artifact.

## Migration data

contributor-mapping.json contains original path/anchor to current destination mappings for the lead’s global link migration. No forwarding documents are retained.


## Final source corrections and onboarding

- Native setup commands match dependency lists and pinned-prefix construction in `.github/workflows/ci-macos.yml`, `.github/workflows/ci-linux-build.yml` and the MINGW64 lane in `.github/workflows/build.yml`. `scons/build_layout.py`, `tools/dev_build.py`, `tools/build_paths.py` and `test/run_tests.py` establish explicit output paths and the required nonrelease runner override.
- Browser fixture README checked against `ReplayReader.h` (minimum 152), `Version.h` (152), retained fixture manifests and corresponding browser specs. Fixture reproduction remains reference documentation; no fixture was regenerated in this docs change.
- JavaScript API paragraph claimed current save 125/network 48/replay 123. Replaced stale copies with links to live version gates rather than another duplicated version inventory.
- Split independent settings, preview and audio tests out of unrelated AI/persistence sections.
- Added maintenance, licensing and lobby automation navigation.

- `browser/package-static.py` reads shared content-addressed `assets/*.data` packages; corrected obsolete single-data-file packaging description. Native test README now supplies an explicit build directory and runner override for fast builds.

## Page-level source anchors

| Page | Primary source anchors |
| --- | --- |
| development/building.md; build-options.md; development-storage.md | tools/dev_build.py; scons/dev_build.py; scons/build_layout.py; tools/dev_environment.py; CI native dependency install steps |
| development/conventions.md | AGENTS.md; .clang-format; tools/remove-unused-includes.py |
| architecture/overview.md; ai-observations.md | Game_sync.cpp; EngineRun.cpp; SimulationRunner.h; SnapshotStore.h; BufferPool.h; ComputeExecutor.h |
| architecture/simulation.md; resource-growth.md | MapStateView.h; TerrainProperties.h; CellRules.h; ResourceGrowth.h/.cpp; MapGradientScheduling.cpp |
| architecture/rendering.md; software-rendering.md; zoom-detail.md | EngineRun.cpp; src/render/scene/; FogFade.h; SoftwareRenderBackend.cpp; SurfaceRaster.cpp |
| architecture/persistence.md; development/savegame-continuation.md; untrusted-inputs.md | DeferredStream.h; FileManagerGzip.cpp; MapHeader.cpp; Version.h; save-format harness registry |
| development/rendering-benchmarks.md; software-rendering-benchmarks.md | test/tests.py PROGRAMS and LEGACY_ALIASES; tools/software_render_benchmark.py |
| development/gradient-benchmarks.md; memory-benchmarks.md; compute-benchmarks.md | tools/gradient_benchmark.py; tools/test_gradient_benchmark.py; tools/memory_benchmark.py; test/check_parallel_compute.py; test/check_sim_thread.py |
| development/package-size.md; media-dependencies.md | tools/package_assets.py; tools/release/package_sizes.py; scons/recording_dependencies.py; scons/mac_image_dependency.py; MusicBuffer.h |
| development/performance-telemetry.md; network-telemetry.md | src/net/turn/TurnTelemetry.h; platform/packages/protocol/src/network.ts; EngineRun.cpp; BuildingGradientStats.cpp |
| development/headless-replays.md | GlobalContainerArgs.cpp; Headless.cpp; src/ai/AI.cpp; ReplayReader.h; ReplayWriter.cpp |
| development/ui-framework.md | libgag/include/ui/; src/ui/; EngineRun.cpp |
| development/verification.md; testing/ci.md | build.yml; ci_policy.py; ci-compatibility.json; test/run_coverage.py; test/run_tests.py |
| scripting/javascript.md; javascript-api.md | src/scripting/javascript/; examples/javascript/glob2.d.ts; GameHeader.h/.cpp |
| browser/implementation.md; audio.md; storage.md; viewport.md; gateway.md; ADRs | browser/ApplicationHost.cpp; browser/storage.js; browser/canvas-size.js; browser/music-worker.js; browser/music-output.js; browser/package-static.py; scons/web_build.py |
| browser/README.md; browser/tests/fixtures/README.md | browser/toolchain.json; browser/package.json; browser/tests/*.spec.js; fixture manifests; ReplayReader.h |
| test/README.md; testing/README.md; testing/*.md | test/tests.py; test/run_tests.py; referenced production-domain Test/Harness sources; test/check_*.py and test/test_*.py |

All new hubs were checked for local destinations and reachable ordered routes. Typed scripting reference remains one API contract; its size reflects record and descriptor tables rather than mixed audiences.

## Final editorial sweep

- Split growth harness methodology out of production architecture; separated resource-corpus benchmarking and skin export/diagnostics from broad pipeline/rendering guides.
- Replaced old asynchronous-migration acceptance narration with reusable paired-comparison methodology. Confirmed component-pool bound 22 against BufferPool.h, and resource-plane 17 epochs against SnapshotStorage.h; corrected obsolete blanket 17-slot claim.
- Removed old tick-rate and FPS migration narrative while retaining current timing/ownership. FogFade::DARKEN_TICKS=37.5 lasts 1.25 seconds at current 30 TPS; corrected stale 1.5-second claim.
- GenerationService.cpp seeds map.worldRandom directly and GenerationContext.cpp owns named request-seeded streams. Removed the obsolete claim that generation saves/restores synchronized global RNG.
- Replaced stale current scripting format127/protocol50/replay127 values with live gates and explicitly historical format transition.
- Removed copied five-topology migration status text and its broken “below” reference; kept golden/table compatibility and reproducible topology comparison guidance. Fixed custom-build commands whose documented output assumed build/native-tests.
- Replaced missing-context “below”, “master” and draft-market narration with actual destinations, retained references and canonical experiment documentation.

- Confirmed `DEFAULT_TICK_RATE_MILLIHZ=30000` in GameTiming.h; browser README now states 30 Hz default, while explicit historical fixture timing remains unchanged. Final editorial checker: 303 documents, zero link/navigation errors.

## Source-archive installation entry point

- Rewrote root extensionless INSTALL as Markdown while preserving its SConstruct source-tar path. AUTHORS attribution was untouched by this task. INSTALL links canonical native builds, Linux install contract and release workflows; includes repository links for source archives that omit docs/.
- Removed obsolete csh/System V advice, old wiki/MSYS pointers, standalone duplicate dependency lists and misleading out-of-source compilation promises.
- Added the Linux install contract to development/building.md from SConstruct option defaults and install aliases, src/SConscript executable install, data/SConscript desktop integration, scons/runtime_assets.py asset destination and libgag/src/FileManager.cpp prefix/data search. BINDIR and INSTALLDIR are destinations; DATADIR is an independent compiled fallback, not an automatically synchronized option.
- Reviewed Linux SDL runtime staging/RPATH in SConstruct: selected prefix libraries install under the executable prefix lib/glob2; distribution system dependencies remain separate. Installation was not executed during this documentation change.
