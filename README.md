# PR766 premade-map upload lookup verification

Final tested commit: d62882a8da9dfad7075b50aa51d8c17cba5473c9. Base: 934c3c588bea51ec7421152e15f5168b2aaf5502. Native release engine rebuilt on the PR head including current master colony-skin changes.

Cause: #760 changed OnlineFakes::Http endpoint matching to ignore URL queries unless the requested suffix contains a question mark. The old upload test supplies only an ampersand filename suffix and cannot find the pending upload. This is deterministic test-fixture misuse, not a production upload failure. Find the canonical endpoint and independently check the exact encoded filename suffix. All existing method/body/MIME/map-choice/server-title/rules assertions remain.

Retained failures: master0d26564bc27d547740b9fbda1395df667d5f1bb8 run37265399142 jobs111627736153,111623638011,111627736182. Excerpts retained in this branch, complete runs preserved on GitHub. Local baseline binary head5c10463b59da95f060da9e1d856cd664c7f190c5 also reproduces precisely REQUIRE(upload) failure; baseline report/log retained, explicitly older binary not claimed as final head verification.

Environment: Linux x86_64 Ubuntu26.04.1, GCC15.2.0, pinned SDL3.4.16/SDL_image3.4.6/SDL_ttf3.2.2; release C++20 -O3 -s -fPIC with native SDK RPATH. Pinned private encoder Pillow12.2.0/libwebp1.6.0. Full flags/provenance in build and test logs. See environment.txt.

Exact build command:
CCACHE=1 GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python scons release=1 build/linux/client/release/test/glob2-engine-tests -j8

Test command from the PR checkout:
GLOB2_TEST_SOURCE_ROOT=$PWD GLOB2_TEST_ARTIFACTS_ROOT=$PWD/artifacts/map-upload-query/final LD_LIBRARY_PATH=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix/lib build/linux/client/release/test/glob2-engine-tests --test-suite=PlatformRoom --reporters=junit --out=artifacts/map-upload-query/final.xml

Coverage: entire affected room suite plus adjacent fake HTTP/client/cache/catalog suites. Test-only change with no production simulation/save/replay/network or gameplay effect. MacOS, Windows, browser, full engine matrix and live server uploads not claimed locally; unchanged production code does not require broad simulation checksum testing for this test lookup correction. Hosted master will confirm recovery asynchronously. Results and acceptance added after verification.

Final results: native engine and unit builds both exit0. Engine PlatformRoom:6cases81assertions0failures0errors0.738648s. Unit PlatformClient,MapCache,MapCatalog:33cases368assertions0failures0errors0.008397s. Unit command same environment with glob2-unit-tests --test-suite=PlatformClient,MapCache,MapCatalog --reporters=junit --out=artifacts/map-upload-query/unit.xml. Total39cases449assertions. Fresh master fetched before final verification remains934c3c588bea51ec7421152e15f5168b2aaf5502, so final tested head directly integrates current master. Author accepts this focused evidence under AGENTS.md; no claimed full hosted pass.
