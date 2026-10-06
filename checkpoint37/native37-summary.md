# Native renderer and HUD checkpoint 37

Revision `7fe776834664300abf24270ed2ac16822885cf5c`; focused runner exited 0. **38 passed, 0 failed, 0 skipped** in 165.6 seconds of test time.

- GUIInteractionCoverage: 2 passed
- TerrainMaterials: 17 passed
- TerrainPresentation: 14 passed
- TorusRender: 5 passed

Exact command and binary SHA-256 are in `provenance.json`; selected inventory, full runner log and JUnit report are retained. The tracked tree stayed clean and the native engine-test binary was unchanged. Tests used the normal isolated display runner, including software, OpenGL and triple-scale OpenGL cases.

Both new HUD fixtures passed. Screenshots show bounded mixed-service markers and the omitted instant-service row; real downstream production-slider hit testing and unchanged simulation checksums are asserted. This is presentation coverage using injected Scene clocks, not a replacement for unit-service lifecycle tests.

Detailed/overview shore captures and the native torus image were inspected. The overview torus capture is mostly dark in the fixture gameplay view; no claim of whole-map visibility follows from it. The terrain-layer capture deliberately contains painted magenta source pixels for cache invalidation testing. Original BMPs are retained, with pixel-identical PNG inspection copies verified in `visual-review.json`.

No source edits, reference regeneration, broad additional tests, or performance workloads were performed. The test session has exited and this lane is quiet.
