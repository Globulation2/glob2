# Bulgarian browser game final evidence

Actual ordinary release artifact producer: `ea0f763fa84445c6dbe5527c966e1eeafc221bc6`.
Final source closure: `f30e5164359621ce7bdcc0a588e3ad274431f16b`; only the expressly authorized online E2E test file differs. No embedded Git SHA is claimed in release JavaScript/WASM. Root explicitly authorized final SCons session 99082 rc0, stable both-variant artifact freeze, staging and execution.

`handoff.json` binds source inputs, both release runtimes, packaged data, localization files and staged package bytes. Package-static and --verify both passed. Runtime package version is `2d5935618dc2c62f`; all 42 locale catalogs and both WASM files were fetched over actual HTTP and matched the frozen bytes. Browser paths/modes and VFS Bulgarian catalog are independently recorded in actual runtime reports.

The tracked four Bulgarian Playwright cases passed together, with one worker and a documented ignored-config 90-second main-menu allowance for this host. Actual results: startup2.2s, serial27.0s, threaded58.9s, English-browser manual Bulgarian selection and persisted reload43.7s. No retries were applied to the tracked suite.

Startup desktop DPR1/DPR2 and phone captures deliberately fail core data503 before WebAssembly. CDP confirms all33 rendered status glyphs use the actual custom Glob2 Sans webfont. The decoded startup font matches its frozen SHA, has font/ttf MIME and gzip encoding; licenses were served200. Standalone `/theme.js` and decorative `/signin/assets/` branding still return404. Those inherited package omissions are preserved in actual network evidence and screenshots; no replacement assets were mounted.

The completed image index contains48 actual screenshots: startup3; main/settings6 across desktopDPR1, desktopDPR2 and phone; normal game39 across desktopDPR2, phone100% and phone150%. Normal game captures include tutorial catalog, briefing, HUD, expanded help, Statistics overview/history/world, new-map/editor, actual valid/invalid JavaScript compilation and cursor. Phone Statistics was reached through a visible Статистика lens tapped at its inspected image coordinates. No observer/postgame visibility flags, simulation state or production source were modified.

Preserved QA failures: the first scene driver requested a game/panel/2 diagnostic control absent from the tutorial observer; the corrected driver uses the already-proven normal right-click path. Supplemental response-body collection hit Chromium inspector cache eviction; the buffered-upstream route proof records the exact actual WASM bytes passed unchanged into the browser. The main/settings reports preserve inspector bodyError diagnostics while independently recording correct actual loaded paths, modes, full VFS font/catalog identities, and no pageerrors.

Environment: Linux Chromium153.0.8010.12, Node24.22.1, repository Playwright installation. Physical Windows/macOS/Android/iOS devices were unavailable and are not claimed. Future revisions are not inferred from this evidence. Compilation coverage is distinct from the actual captured browser rendering.

Reproduction entry points: `freeze.py` (requires explicit root authorization); `stage.py`; `gzip-server.py` on127.0.0.1:42771; repository Playwright CLI with `playwright.config.cjs`; `startup.cjs`; `matrix/capture.cjs`; `scenes-retry/scenes.cjs`; `variant-proof-route.cjs` and `variant-proof-serial.cjs`. Actual command histories and JSON contain the precise hashes and results. Source/build scripts are unchanged by this worker.
