Source head: d7895a77ee6e375b857c993c05831b4d63c7a138, branch base842a208e65ac7c9f6ee9fa633504d5525a277bda. Native/Wasm engine sources are not changed. The retained Wasm build is clean697ad9d29ed4c2c7d5c965c71dd4ad54395e8306 from full master run40556771805. Corpus probes check that producer identity explicitly. The unchanged DelayedGradientPublicationHarness.cpp matches that producer; final-source hosted guards remain intact and still need a rebuilt hosted run.

- Serve: python3 browser/serve.py 8779 --bind 127.0.0.1 --directory artifacts/ci-watch/2008/web-client
- Software: GLOB2_TEST_URL=http://127.0.0.1:8779 GLOB2_TEST_BUILD_DIR="$PWD/artifacts/ci-watch/2008/web-client" browser/node_modules/.bin/playwright test render-fps.spec.js responsive-presentation.spec.js viewport.spec.js --config=browser/playwright.config.js --project=chromium
- WebGL2: GLOB2_TEST_RENDERER=webgl2 GLOB2_CHROMIUM_ANGLE=swiftshader GLOB2_TEST_URL=http://127.0.0.1:8779 GLOB2_TEST_BUILD_DIR="$PWD/artifacts/ci-watch/2008/web-client" browser/node_modules/.bin/playwright test render-fps.spec.js responsive-presentation.spec.js viewport.spec.js --config=browser/playwright.config.js --project=chromium --grep 'render FPS selection persists \(compact\)|native text editing preserves|menus follow the viewport' --output=artifacts/ci-watch/2103/webgl-results --reporter=list
- GLOB2_TEST_BUILD_DIR="$PWD/artifacts/ci-watch/2008/web-client" node artifacts/ci-watch/2103/probe-inventory.cjs
- GLOB2_TEST_BUILD_DIR="$PWD/artifacts/ci-watch/2008/web-client" node artifacts/ci-watch/2103/probe-gradient-corpus.cjs
- node --test browser/unit/*.test.js
- python3 -m unittest discover -s test/build_system -p 'test_ci*.py'
- python3 tools/check_docs.py
- node --check browser/tests/compiled-case-inventory.js
- node --check browser/tests/determinism.spec.js
- git diff --check

The local corpus probe extracts the unchanged runtime capture block from the committed browser test, then verifies exit status, failure/error/skip absence, exact registry membership, kernels, every boundary row and serial/threaded trace identity against the retained producer. It does not bypass or modify the committed current-source guard, and is not a final-revision full hosted result. Original hosted JUnit and manifests, fresh inventories/results and screenshots are attached. The first probe mutation attempt did not change self-closing XML and failed its expected-rejection assertion; the corrected probe and complete final results are retained here.
