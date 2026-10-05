# PR 758 native/browser PNG verification

Head 94033f1892e165b794b9b123ce97b783f8bdb911; base e7f249f41a5f354f696e9d65abc0a243a4556435. Linux x86_64, GCC 15.2.1 native SDK build, pinned Emscripten 4.0.15 SDK, Node 22.22.1. See toolchain.txt for actual tool versions.

Original hosted browser failures: https://github.com/Globulation2/glob2/actions/runs/37255462043/job/111600108498 ; retained tests.xml in browser-determinism-wasm-0 reports exact ImageAssets 16-bit RGBA failure for both serial and threaded cases. The unmodified native SDL fallback reproduces the same off-by-one RGB/alpha bytes (before.log).

## Reproduction and verification

Extract the checksum-pinned SDL3-3.4.16.tar.gz from scons/sdl3-versions.json into artifacts/png16/native-source. Apply `sdl3_dependencies.apply_source_patches` twice; patch-application.log checks clean/idempotent application and verifies byte-identical built stb_image.h. Build native SDK with:

`cmake -S artifacts/png16/native-source/SDL3-3.4.16 -B artifacts/png16/native-build -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DCMAKE_INSTALL_PREFIX="$PWD/artifacts/png16/native-prefix"`
`cmake --build artifacts/png16/native-build --parallel 8`
`cmake --install artifacts/png16/native-build`

For WebAssembly use the pinned SDK's emcmake with the same source, wasm-build/wasm-prefix paths, `-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_PTHREADS=OFF -DCMAKE_C_FLAGS=-sSUPPORT_LONGJMP=wasm -DCMAKE_CXX_FLAGS=-fwasm-exceptions`, then build/install.

`g++ artifacts/png16/check.cpp -Itest/support -Iartifacts/png16/native-prefix/include -Lartifacts/png16/native-prefix/lib -Wl,-rpath,"$PWD/artifacts/png16/native-prefix/lib" -lSDL3 -o artifacts/png16/check-native`
`artifacts/png16/check-native`: all 16 expected fixture bytes match.

`em++ -fwasm-exceptions artifacts/png16/check.cpp -Itest/support -Iartifacts/png16/wasm-prefix/include artifacts/png16/wasm-prefix/lib/libSDL3.a -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -o artifacts/png16/check-wasm.js`
`node artifacts/png16/check-wasm.js`: all 16 expected fixture bytes match. Same production SDL fallback and fixture as the failing browser test; exact expected RGBA bytes unchanged.

Against the freshly patched native shared SDL, exact GCC 13.3 unit binary from master run 37255462043 passes all five original ImageAssets cases. Use `GLOB2_TEST_SOURCE_ROOT=$PWD`, `GLOB2_ASSET_DIR=<matching 99958 runtime export>`, `LD_LIBRARY_PATH=<new SDL prefix/lib>:<hosted SDL prefix/lib>`, then `python3 test/run_tests.py --binary unit --build-dir <restored hosted build/linux/client/release> --filter 'ImageAssets/*' --verbose --artifacts artifacts/png16/native-unit`. This verifies unchanged WebP alpha/RGB and native PNG/JPEG paths; it does not claim recompilation of the expanded test source. The newly expanded direct fallback is independently executed by check.cpp in both native and wasm builds.

Dependency contracts: private pinned encoder Python with `-m unittest discover -s test/build_system -p test_sdl3_dependencies.py -v`: 3 tests, one unrelated absent SDL_ttf archive skip. Complete build contracts are also running; final result will be recorded before merge.

## Scope and limits

No test expectations are weakened. Every source patch identity is hashed into dependency caches; vcpkg applies the same patch. No bundled assets, simulation rules, prepared maps or save/replay/network formats change. External high-depth PNG channels now round consistently rather than truncate. Actual browser-suite runners and Windows/macOS/Android complete engines were not run locally; hosted affected checks are requested. PNG numeric decoding is exercised in actual WebAssembly through Node, not a JavaScript approximation. Full master CI will establish matrix recovery.
