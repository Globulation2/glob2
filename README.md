# JavaScript map generator review and verification

PR: https://github.com/Globulation2/glob2/pull/958

Native implementation verified at 681f6b85dfea219837d59327ce5cca49d37a8afd, on master da57b459f1eb20b9b4a08d25eb502711dd81986b. Final PR head a00af7fe1 adds only the CI invocation for the already-tested TypeScript authoring contract. Native source, compiler, dependency and build-flag inputs are unchanged; that head was rebuilt and 31 focused engine cases rechecked. Latest fetched master95d06cf86 adds two unrelated Maxima checksum fixture corrections; no source/build integration changes or conflicts.

Linux x86-64, GCC15.2.0, Python3.14.4, TypeScript7.0.2. SDL3 uses the locally patched pinned SDK; strict generator numeric flags are -fno-fast-math -ffp-contract=off. See environment.json and build logs for complete identities. optimized_assets=0 uses verified lossless runtime exports, avoiding unrelated optimized artwork work.

## Independent review rounds

Three reviewers audited runtime ownership/budgets, generated API contracts/tooling, and catalog/library/UI integration. First-pass fixes: moved arguments no longer retain addresses into temporary storage; owned classes retain borrowed context/RNG leases; operation registrations and native copies share resource accounting; catalogs stay frozen for selected requests; room pickers use native catalogs; package persistence changes memory only after atomic storage succeeds. The package parser, binding runtime and emitter were split into focused operations with explicit contracts and maintained documentation.

Second-pass fixes: empty fertility inputs fail before native assertions; mutable controls validate bounded positive-step domains and safe shifts; direct and indirect fertility kernels reserve work/scratch; nested buffer get/fill/set and graph closures reserve payloads; dispatch shares callable storage; TypeScript mutable numeric buffers carry storage brands. Closure validation repaired obsolete preference fixtures and factory calls, and made budget diagnostics retain the first cause after a script catches an exception. Native/Python packaging preserves identical CRLF module payloads; both reject symlinked authoring roots. All reported blockers are resolved.

## Commands and results

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-terrain-sdl-patched/prefix scons -j8 release=1 server=0 optimized_assets=0 engine-tests unit-tests mobile-gallery map-generator-golden-test build/linux/client/release/src/glob2
python3 test/run_tests.py --binary engine --filter 'ScriptGenerator/*' --filter 'JavaScript*/*' --filter 'MapGeneratorDefaults/*' --filter 'default generation repeatability*' --filter '*Save*/*' --filter 'TurnEngineHarness/*' --filter 'MapGeneratorRegistry/*' --filter 'ResourceGrowth/*' --filter 'CustomGameSetup/*' --filter 'MatchSetup/*' --filter 'UnitContinuation/*' --filter 'MapGradientInvalidation/*' --filter 'UntrustedFiles/*' --no-display -j8 --junit artifacts/generator-accepted-engine.xml
python3 test/run_tests.py --binary unit --filter 'JavaScript*/*' --filter '*Resource*Registry*/*' --filter '*Golden*Coverage*/*' --filter 'FertilityField/*' --filter 'TerrainRegistry/*' --filter 'SkinMaterialMap/*' --no-display -j8 --junit artifacts/generator-accepted-unit.xml
python3 test/run_tests.py --binary unit --filter 'generator golden coverage*' --no-display --junit artifacts/generator-accepted-golden-coverage.xml
build/linux/client/release/src/MapGeneratorGoldenTest artifacts/generator-accepted-native-profile --require-rows
python3 test/run_tests.py --binary engine --filter 'ScriptGenerator/*' --filter 'CustomGameSetup/custom AI library*' --filter 'CustomGameSetup/preferences; landscapes*' --filter 'TurnEngineHarness/the committed*' --no-display -j8 --junit artifacts/generator-accepted-final-head.xml
```

All exit0: 285 engine cases passed,0 failed,13 display-only cases skipped (298 cases);71 selected unit cases plus1 golden-coverage case passed;564 native golden rows matched. Final-head focus:31 passed. JUnit/logs retain individual results and native build provenance.

```sh
PYTHONPATH=artifacts/binding-tools GLOB2_SDL3_PREFIX=/tmp/glob2-terrain-sdl-patched/prefix python3 tools/map-generators/generate_js_bindings.py --check
artifacts/typescript-tools/node_modules/.bin/tsc --strict --noEmit test/map_generator_toolkit_contract.ts
python3 test/check_sim_revision.py --base origin/master
python3 data/check_translations.py --strict
python3 tools/javascript/verify-vendor.py
PYTHONPATH=artifacts/binding-tools python3 test/test_toolkit_instrumentation.py
python3 test/test_generator_package_tool.py
python3 test/test_tournaments.py
python3 test/test_tournament_pipeline.py
python3 test/test_map_generation_study.py
python3 -m unittest discover -s test/build_system -p test_ci_policy.py -q
```

All exit0. Tool cases: AST3,package3,tournaments43,pipeline13,study6; CI policy21. Build-system359 cases pass with2 skips using the pinned asset encoder Python (the interpreter reported by `python3 tools/package_assets.py --encoder-python`). The initial system-Python attempt had encoder-related failures; the pinned run resolves them. Vendor reproduction: existing patched QuickJS22 files and Openlibm70 files. Generated adapters/TS declarations and translations pass. CI now invokes the package, AST and TS authoring contract checks in the existing native tooling auxiliary group, using clang18.1.8/libclang18 and TypeScript7.0.2 development tools.

```sh
python3 artifacts/generator-review-jobs.py
python3 artifacts/generator-review-sweep.py
build/linux/client/release/src/glob2 --verify-match artifacts/generator-review-base-orders.g2mr --map maps/FourSquares1.map.gz --out artifacts/generator-review-base-orders-final
cmp artifacts/generator-review-base-trace.txt artifacts/generator-review-base-orders-final/checksums.txt
```

All exit0. Frozen-package EngineJob generate_map and game pass: Swamp seed91,256x256,2teams,Cortex/Maxima,game seed713,512ticks. Loading the saved game without the package and continuing to tick1024 matches a continuous run from the initial save: checksum2283868378. No per-tick continuation comparison is claimed. Request, exact CLI command, map, saves, replay, result and checksum comparison are attached under game/.

The match record preserves master's setup/orders, changing only the setup and record's sim-version hash and CRC. Its702 checksums (ticks0..701) match master's trace exactly. Source SIM_REVISION36 and the committed regenerated record pass the sim-version gate. See match/.

Four script sweeps pass at seed91: Swamp128x256/2teams,Forts512x256/4,EvenGround256x512/4,Forts512x512/4. Exact commands, requests/reports, portable packages, maps and previews are in examples/. Native/Python CRLF-package identity and native root-symlink rejection pass under package-contracts/.

```sh
GLOB2_GALLERY_GENERATORS_ONLY=1 GLOB2_USER_DATA_DIR="$PWD/artifacts/generator-review-gallery/desktop" SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software build/linux/client/release/src/mobile-gallery 1280 800 desktop
GLOB2_GALLERY_GENERATORS_ONLY=1 GLOB2_USER_DATA_DIR="$PWD/artifacts/generator-review-gallery/phone" SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software build/linux/client/release/src/mobile-gallery 393 852 compact
```

Both exit0 after installing the three example packages in the isolated gallery library. All six captures were inspected: package list, scripted editor preview and local-game preview at desktop/compact sizes. Files are in gallery/.

## Coverage and limitations

Coverage follows the changed risks: frozen catalogs and persistence, package parser/import contracts, shared-toolkit data validation, owned/borrowed lifetimes, callback behavior, deterministic resource accounting, generated API declarations, unchanged built-in generation, save/load continuation, replay/network acceptance and simulation-version boundaries. Built-in map feel is intentionally unchanged; no layout/balance tuning is claimed for the optional examples.

No Windows/macOS/ARM64/browser/mobile-runtime equivalent-execution run was available locally. No human gameplay playtest, extensive economy calibration, statistical control study or hosted expensive matrix was requested/performed. Gallery compact captures use desktop software rendering at phone dimensions, not an actual mobile device. Hosted cheap contracts do not establish engine/platform compatibility. These omissions are explicit; cross-platform checksum equivalence remains unverified locally.

Maintainer acceptance: merge proceeds under Bradley's explicit authorization after independent review rounds, fixes and this verification. A second maintainer approval is not required by the repository policy.

## Skipped display cases

- CustomGameSetup/landscape preview performance [display][artifacts]
- CustomGameSetup/landscape preview stays responsive [display][artifacts]
- CustomGameSetup/controller help is localized with the current player capacity [display]
- CustomGameSetup/custom game screens; captures and translated keys [slow][display:1024x768][artifacts][writes-preferences]
- CustomGameSetup/probability statistics fit sixteen colonies [display][artifacts]
- CustomGameSetup/AI profile captures [display][artifacts][writes-preferences]
- CustomGameSetup/player control widgets [display][artifacts][writes-preferences]
- CustomGameSetup/AI strategy profiles in English [display][artifacts][writes-preferences]
- CustomGameSetup/AI strategy profiles in English at the large layout [display:1024x768][artifacts][writes-preferences]
- CustomGameSetup/preferences screen writes then reads back [display][writes-preferences]
- JavaScriptIntegration/JavaScript custom library and telemetry dialogs render [display:1280x800] [artifacts]
- JavaScriptIntegration/JavaScript terrain resource permissions follow both catalogs and charge full enumeration [display]
- TeamStatsSave/measurement screenshots [display][artifacts]
