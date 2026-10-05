# Markets V2 validation

Implementation revision: `f5fa88e9175fa0014e46f6a36b153ad4cf5b9205`.
Final revision: `bbb793b97c7eb7e0139b5fd95ecbaafa2bce1cda` (only adds shared-stock withdrawal/arrival assertions).
Base: `79c8d65f52cfea0a91efa3c104f97d79fd5de54d`.

## Scope and results

The disabled trace matches a separately built master across 3,000 ticks: serial and two-worker fixed-delay gradients, fruit trading, attached workers and binary/text save continuation. The retained worker in the genuine format-133 fixture also matches current master across 250 continuation ticks. The heavy per-tick state vector comes from Game::checkSum; the MapHeader checksum is excluded because it contains the intentionally different save-format number (134 versus 135). Team/unit/building/map simulation parts are compared. This is a controlled 64x64 two-team seed-271 scenario with growth disabled and identical scripted stock deliveries; it does not prove equivalence for every possible game.

Final focused cases cover each market level/resource/swim combination, natural versus stocked sources, retained attachment, shared-stock depletion during travel and restocking, forbidden/water routes, upgrade/cancellation, repair stock preservation, unavailable direct upgrades, binary/text persistence and pending fixed-delay publications. Final replay tests directly exercise old/current/future replay acceptance and released save migration. The multiplayer golden verifier and sim-version policy pass. Desktop/touch extracted panels hide upgrades and levels with V2 disabled; PNGs show both settings.

The broader macOS quick/no-display run at f5fa88e91 has 1,363 passing cases, one failing case and 111 skips (including 571 engine passes and 792 unit passes). The failure is ImageAssets/16-bit RGBA decoding; the same two pixel assertions fail on freshly built current master with the same pinned SDL libraries (baseline-image.log/xml). Strict translation audit passes with zero structural errors and no unapproved fallback. The separate test_translations.py suite fails 192 fallback assertions, identically on master, for six newly added terrain strings in 32 non-English catalogs; these strings are explicitly pending in master. Markets V2 label/help are translated in every catalog.

## Environment and commands

macOS arm64, Apple LLVM 21, C++20, release O3, SDL3 3.4.16 pinned prefix, Python 3.14.7, SCons and ccache. Full compiler flags and clean source hashes are in build-provenance.json and logs. Dependencies use vcpkg.json/build scripts plus Homebrew OpenSSL, epoxy, fribidi, speex, opus/opusfile/ogg and bundled recording dependencies. See metadata.json.

```
GLOB2_SDL3_PREFIX=/Users/bradley/.cache/glob2-sdl3/prefix CCACHE=1 scons -j6 release=1 tests build/darwin/client/release/src/glob2
GLOB2_SDL3_PREFIX=/Users/bradley/.cache/glob2-sdl3/prefix CCACHE=1 scons -j4 release=1 role=relay
python3 test/run_tests.py --build-dir build/darwin/client/release --quick --no-display --junit artifacts/markets-v2/final-engine.xml --artifacts artifacts/markets-v2/final/engine
python3 test/run_tests.py --build-dir build/darwin/client/release --filter 'MarketsV2/*' --filter 'MarketFetch/*' --filter 'TurnEngineHarness/the committed*' --filter 'OrderValidation/*' --filter 'Experiment*/*' --no-display
python3 test/run_tests.py --build-dir build/darwin/client/release --filter '*Terrain simulation change rejects*' --filter '*JavaScript pass assigns*' --filter '*JavaScript upgrade retains*' --no-display
python3 test/run_tests.py --build-dir build/darwin/client/release --filter 'MarketsV2/market panels*'
python3 test/check_sim_revision.py --base origin/master
python3 data/check_translations.py --strict
python3 test/run-browser-determinism.py build/darwin/client/release/src/glob2 artifacts/markets-v2/final/native-browser
node --check browser/tests/determinism.spec.js
python3 -m py_compile scons/web_build.py
python3 -m json.tool .github/scripts/ci_browser_matrix.json
```

For baseline reproduction, check out the exact base; apply baseline.patch (adds the test registry entry and the non-simulation text parser repair required for existing qualified statistics keys), copy baseline-MarketsV2Test.cpp to src/unit/MarketsV2Test.cpp, and copy test/fixtures/markets-v2/legacy-133-market.bin from the feature branch. Build engine-tests and generate only `MarketsV2/disabled fruit*` and `MarketsV2/legacy workers*` with --update-fixtures. Enabled code is preprocessor-excluded on master. The baseline has no market simulation changes. Baseline logs and actual generated checksums/saves are retained here.

## Routing overhead

Ten repeated 896-field synchronous refresh workloads on identical 64x64 grass, same eight resources and seven swim classes: median CPU off 0.0121655 s, on 0.014124 s. Market field storage off zero, on 458,752 bytes (448 KiB); this excludes common natural fields and allocator/pipeline overhead. The ordinary supply-network scenario lazily allocates four market fields (32 KiB), versus none off. This small workload is a measurement, not a general performance projection. Raw samples and provenance are under final/benchmarks.

## Review limits

The PR remains draft. Automated supply-network simulation and captured desktop/touch panels do not replace a maintainer playing ordinary trading and depot supply networks for feel. Level-2 market upgrade retains the proposal's 560/600 completion HP and needs repair before upgrading again. All levels reuse one sprite. Existing inter-team exchange controls were already disabled; the fruit-only legacy code is preserved, not revived.

Windows and browser/Wasm deterministic checks are requested through ci:run, ci:windows and ci:browsers; pending hosted checks cannot establish cross-platform equivalence. Final Linux results will be recorded separately. Skipped display/fullscreen/slow cases and balance playtesting remain uncovered. Keep #257/#258 open until this replacement merges, then close them as superseded with links.
