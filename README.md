# Discord community menu verification

Source commit: f39847361c7d602ce78ffba4e6747698ee4b02df
Branch base: 7d17f40d156fa847cc8e585907aee1d13ac6fdd0
Fetched master: 5443f425e0ff00065457ce8baa68238557db49f6
Clean integration tree from git merge-tree: 6a96d3a46e11c16d53184ef8d2520452344e0dca. Newer base work affects the web platform, deployment and iOS release tooling. The build.yml change runs design:sync in the platform job; it does not alter native client compilation or cheap checks. The changed Discord UI and native build/dependency inputs are unchanged in the merge result.

Environment: macOS 26.6.2 (25G83), arm64, Apple Clang 21.0.0 (clang-2100.3.34.2), Python 3.14.7. Native fast development build: dev_fast=1, linker=auto, compiler cache enabled, -O0 -g1. SDL versions reported by pkg-config: SDL3 3.4.18, SDL3_image 3.4.8, SDL3_net 3.2.0, SDL3_ttf 3.2.2. Full compiler and dependency flags are retained in the build log.

Build command: `python3 tools/dev_build.py mobile-gallery`.

The first capture attempt used `build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/mobile-gallery`; Discord/main-menu/More captures and return navigation completed in all three cases, but the broader gallery did not complete. Both compact cases exited 1 waiting for unrelated editor landscape previews to settle; desktop reached the same editor section and hit the 300-second runner timeout. These failures are retained, and no complete-gallery pass is claimed.

Focused capture commands use `artifacts/discord-focused-gallery` with `(1280 800 desktop)`, `(320 568 compact)`, and `(568 320 compact)`. Each command has a separate disposable GLOB2_USER_DATA_DIR, SDL_VIDEODRIVER=dummy, and SDL_RENDER_DRIVER=software. Both capture runners are included. The focused driver is an ignored copy of the committed MobileGalleryHarness.cpp that stops after main-menu/Discord/More captures and the existing canned online-hub fixture, scrolling to and clicking hub/discord, capturing the shared panel, clicking Back and checking that the hub action is restored, skipping unrelated editor, settings, lobby and gameplay screens. Its complete source, exact diff, compile/link commands and log are included. All production source and linked objects are from the tested commit and the same build/dependency configuration; only the capture driver changes. These are native shared-UI captures using synthetic touch presentation, not Android/iOS binaries.

Static checks: all 10 new translation keys occur exactly once in all 33 language tables; gallery catalog JSON parses; git diff --check passes; the new shared component passes clang-format --dry-run -Werror. The documentation checker tool's 10 tests pass.

Hosted cheap checks failed at documentation navigation: docs/mobile/terms.md is unreachable from docs/README.md. The branch documentation check and baseline documentation snapshot both report the identical error (307 documents, 1 error). Only docs/multiplayer/client.md differs from the branch baseline in documentation; the baseline-check wrapper substitutes its original contents in memory. Failure logs and wrapper are included. This is a pre-existing failure, not a pass or engine verification.

Coverage rationale: this change adds UI controls and a shared external-invite panel through existing URL/clipboard host interfaces. No simulation behavior, save state, replay/network acceptance, runtime dependencies, or performance policy changes. Native linking checks the production client; captures check menu discovery, panel layout, and return navigation.

Omissions: no Windows/Linux builds, Android/iOS device runs, browser builds/URL or clipboard handoff checks, release packaging, complete engine test suite, or simulation checksum comparisons. No claim of those platforms or release readiness. Simulation/checksum/save/replay tests are omitted because those boundaries are unchanged.

Asset export reused completed content-addressed encoder cache entries from other worktrees. The existing exporter checks source-byte SHA-256, encoder recipe/dependency versions, output hashes and dimensions, and lossless pixel/alpha equivalence before accepting an entry. This copied cache data only; source assets and build settings were unchanged. Cache-copy scripts and logs are included.

Build results: native mobile-gallery build exited 0; final repeated build exited 0 and reported the target up to date. Visual inspection of the original three Discord captures confirms readable title/body and visible Join, Copy Link and Back controls. The narrow portrait URL field horizontally scrolls its text, and Copy Link uses the full URL. The actual main-menu/More controls opened the panel and returned without errors. The landscape More menu scrolls to reach its Discord action, as exercised by the capture driver.

Final focused run: all three processes exited 0 (desktop 4 captures, portrait 5, landscape 5). Clicking the real main-menu and online-hub Discord controls, then Back, succeeded at every size. Hub captures scroll the existing play pane to reveal the community action on phones. Visual inspection found the shared panel and action controls readable and reachable. The canned hub fixture logs existing missing translation keys for its synthetic map titles; no assertion or navigation failure occurred. External browser/Discord launch and system clipboard handoff were not manually exercised.

Screenshots: [desktop panel](desktop-discord-community.png), [portrait panel](phone-portrait-discord-community.png), [landscape panel](phone-landscape-discord-community.png), [desktop hub](desktop-online-hub.png), [portrait hub](phone-portrait-online-hub.png), [landscape hub](phone-landscape-online-hub.png).
