# PR 759 inventory verification in progress

Final head 5f9ff585e0227449b72d495b3a2e8fec289284b6; base 857b69530254983efe1659b85467a17fa1973dfb. JavaScript syntax passes with `node --check browser/tests/image-assets.spec.js` (Node 22.22.1 Linux x86_64).

Original hosted master 99958 browser-determinism-wasm-0 tests.xml contains the four ImageAssets case names in original-hosted-inventory.txt. PR 758 expands the PNG case into two decoder subcases. The actual pinned doctest reporter produces a separate testcase for each subcase, demonstrated by `g++ -std=c++20 -Itest/third_party artifacts/image-inventory/reporter-check.cpp -o artifacts/image-inventory/reporter-check` followed by `artifacts/image-inventory/reporter-check --reporters=junit --out=artifacts/image-inventory/reporter-check.xml`; all assertions pass and there are two testcase entries, named decoder subcases/SDL_image and decoder subcases/SDL fallback.

Therefore the complete repaired browser codec suite has five JUnit entries, not the old two. The wrapper now explicitly requires the lossy-alpha entry and each of the two PNG decoder entries, while retaining zero exit/failures assertions. Native-only PNG/JPEG saving remains excluded from WebAssembly.

Actual complete Playwright execution against the patched hosted WebAssembly artifact is pending. This partial inventory/reporter evidence does not claim an end-to-end pass. PR remains draft until that verification is recorded.
