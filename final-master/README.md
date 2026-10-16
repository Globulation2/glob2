Final master local verification

Revision a5f3a9e3655d17d0bacdb916c11f88516a7b661f, clean tracked source, Linux x86_64. Emscripten 4.0.15, Node 24.19.0, Playwright 1.63.0, existing pinned dependencies. verification.json retains commands, exact binary SHA-256 values, source and toolchain. Serial/threaded TestBuildProvenance headers record the source identity embedded into both scripting producers.

All 567 build-system contracts pass (2 optional skips). All 324 top-level Python tests pass (3 skips), using the repository's unchanged requirements-dev.txt environment and the pinned SDL3 prefix. The first top-level run omitted GLOB2_SDL3_PREFIX and could not locate SDL3_ttf; that unsuccessful invocation is retained alongside the correctly configured passing run.

The first 24 determinism cases in the full 221-case Chromium run pass: simulation traces, committed match verification, shared scripting corpus, Markets V2, custom building compositions, runtime catalogs, and generator save continuation. Both scripting corpus manifests match clean current source and the embedded producer revision. Every recorded scripting JS/Wasm/data hash has been independently compared with the actual current build; see manifest-integrity.json. Raw outputs and per-tick traces are retained in determinism/.

This fresh matching-source evidence supersedes the automatic binary hashes in the earlier browser-startup archive, whose local output alias pointed at the older downloaded artifact. The earlier executed 6f9 producer source identity and separate manually recorded hashes remain valid; its automatic binary hashes do not. That bookkeeping error and original records remain explicitly retained for audit.

The full Chromium UI run and full hosted matrix are still running. This checkpoint does not claim their success. Existing focused Firefox/WebKit evidence is retained in browser-startup, browser-fixtures, browser-view-fixtures and related folders. Complete local native verification remains in final-verification (1872 jobs / 2968 cases); subsequent affected fixture checks and actual Windows focused verification are retained separately.
