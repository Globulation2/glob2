## WebGL skin shader repair: provisional diagnosis evidence

PR head f211c818a4fe886ce011f6463623236e748f4f3d, base 7302aae6d26937ef477bc8e928d026bb2f058c2c. Ubuntu 26.04.1 x86_64, Node 22.22.1, Playwright 1.63.0. Browser runtime artifact web-client from master run 37238152912, revision 87a8b25284920c00c224786c0cb1930d21f3d337, Emscripten 4.0.15.

This is diagnostic evidence against the exact failing runtime, with the new post-link shim appended verbatim to index.js and threaded/index.js. The underlying WebAssembly and assets are unchanged. It is not a fresh build of current master (which changed audio dependencies); hosted affected verification is requested on PR #740 before final acceptance. runtime-sha256.json records the original/patched scripts and unchanged WebAssembly hashes.

Original failure reproduced. skin-debug.log captures the shader source beginning with #extension GL_OES_standard_derivatives before #version 300 es, despite ready skin assets and zero GL context errors. Existing compileShader silently rejects invalid shaders.

Commands from browser/:
```sh
npx playwright test tests/colony-skins.spec.js --project chromium --output ../artifacts/skin-fixed-chromium
npx playwright test tests/colony-skins.spec.js --project firefox --project webkit --output ../artifacts/skin-fixed-other-browsers
GLOB2_SKIN_TEST_THREADS=threaded npx playwright test tests/colony-skins.spec.js --project chromium --output ../artifacts/skin-fixed-threaded
node --test unit/*.test.js
```
All four rendering/context restoration runs pass, preserving the original draw/count/batching/composite/settings assertions. 44 browser unit tests pass. Live and restored screenshots are attached for all four executions.

Build contracts:
```sh
/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -v
```
310 tests pass, three optional skips. Initial system Python without Pillow moved exporter work into a subprocess and invalidated in-process mocks; its four failures are also retained transparently. The supported managed encoder environment runs these tests in-process and passes.

Limits: fresh hosted link/current Opus dependency integration pending; threaded Firefox/WebKit, native/mobile execution and simulation checksums are not claimed by these provisional runs. No simulation source or orders changed.
