# PR 837 empty BitArray verification

Final tested head 6a56ad443fbe3939b23e43b74296e06b0ffb7509; base 7d61a3c18ce558775c793924782d8747f022c637. Fetched master c13d2702910fae20da5dc69a2fb0a147e482bd27 changes Maxima policy tests/schema help and platform music/Map Studio code relative to this base; no common utility, order decoder, test build or dependency overlap.

Ubuntu 26.04.1 x86_64, Clang 18.1.8, libstdc++ bounds assertions, O0 native coverage. Pinned SDL3 3.4.16 / image 3.4.6 / ttf 3.2.2 / net 3.2.0, WebP 1.6.0, pinned recording prefix; exact archive hashes and repository SDL patch hashes are in provenance.json. System speex/opus/openssl/Boost dependencies as recorded by the attached build log.

Build: PKG_CONFIG_PATH=build/sdl3-ci/prefix/lib/pkgconfig scons -j16 release=0 server=0 CC=clang-18 CXX=clang++-18 CXXFLAGS='-g -O0 -fprofile-instr-generate -fcoverage-mapping -DGLOB2_TEST_COVERAGE' LINKFLAGS='-g -fprofile-instr-generate' --build=build/native-coverage-torus engine-tests unit-tests. Actual compiler/link lines retained in final-build.log.

Run with LD_LIBRARY_PATH=build/sdl3-ci/prefix/lib:
- python3 test/run_tests.py --build-dir build/native-coverage-torus --binary engine --filter 'OrderValidation/*' --jobs 4 --junit artifacts/capability-order-fuzz-repair/order-refreshed-junit.xml --artifacts artifacts/capability-order-fuzz-repair/order-refreshed-artifacts
- python3 test/run_tests.py --build-dir build/native-coverage-torus --binary unit --no-display --junit artifacts/capability-order-fuzz-repair/unit-refreshed-junit.xml --artifacts artifacts/capability-order-fuzz-repair/unit-refreshed-artifacts
- python3 test/run_tests.py --build-dir build/native-coverage-torus --binary unit --filter 'BitArray/*' --no-display --junit artifacts/capability-order-fuzz-repair/bitarray-junit.xml

The original fixed-seed 8000-round order decoder fuzz abort is preserved in backtrace-corrected.log and the original order log. Empty valarray element zero was accessed merely to form the zero-length copy pointer. The guards avoid this undefined access while retaining reset behavior. All three BitArray cases pass, including no-buffer empty serialize/reset and exact unchanged 0xA1,0x11 odd-length bytes; all eight order-validation cases pass, including hostile orders and the previously aborting fuzz.

Initial broader unit run encountered PNG rounding because the reusable local SDL prefix lacked the existing repository png16-preserve-channels patch. Standalone image-decoder-probe reproduces this without any application/BitArray source. Existing repository patches were applied through scons.sdl3_dependencies.apply_source_patches, then cmake --build build/sdl3-ci/sources/SDL3-3.4.16-build -j8 and cmake --install that build refreshed the prefix. The identical probe then passes. Both initial and refreshed logs are preserved. This is an environment correction, not a repository/test expectation change.

This repair changes no accepted orders, computation for valid input, encoded bytes, save format or replay/network acceptance gates; SIM_REVISION remains20 and no golden trace change is required. Coverage targets empty-buffer/reset behavior and its real order-decoder caller plus shared-unit integration. No cross-platform checksum equivalence, display tests, full engine matrix, Windows/Mac/Wasm run, or gameplay review is claimed. Hosted cheap contracts alone are not native verification.

Final refreshed dependency results: all 833 headless unit cases pass, 18 display/benchmark cases skipped (851 JUnit inventory); all eight order-validation cases pass. Full unit process exit0, 101.4seconds; order suite exit0, 11.5seconds.
