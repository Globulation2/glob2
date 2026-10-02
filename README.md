# Application size optimization implementation

Worktree: image-compression-study; baseline 75ad507e7851ee0b536331dfa1e7743e2f12639b.

Implemented shared package-time PNG/WebP selection and source-preserving exports; quality-85 lossy WebP only for the opaque menu illustration. Native image lookup keeps directory priority and checks the original PNG before WebP. Windows, Linux release installs, Mac bundles, WebAssembly and mobile release packages use the exporter. Linux store icons/screenshots keep PNG. Linux upgrades track managed files in a compressed index.

Mac release copies retain matching dSYMs before stripping, use pinned lean SDL_image and canonical dylibs with required SDL3 aliases. Browser releases use pinned WebP/SDL_image ports, immutable versioned names and deterministic gzip sidecars. Caddy and Storage paths serve the correct original MIME/Content-Encoding. Source codec notices and durable guides updated.

## Verified evidence

- 130 build-system Python tests pass, including actual SCons reinstall and export rollback (artifacts/size-study/final-build-system-tests.log).
- 7 Windows packaging helper tests pass (windows-package-tests.log).
- 16 browser JavaScript unit tests pass (browser-unit.log).
- Caddy 2.10.2 identity/gzip negotiation and decompressed content pass for HTML/JS/WASM/data (http-compression.json); this is a local server test, not deployed hosting evidence.
- SDL_image lean decoder pixel-exact comparison passes for 6815 lossless pairs, including invisible RGB (final-sdl-compare.json). Single sequential timing is not a controlled performance benchmark.
- Same-filter asset ZIP9: 42701760 -> 31846672 bytes (final-assets.json). This compares asset-only archives, not store downloads.
- Original menu illustration and quality-85 WebP inspected visually; background RGB is intentionally lossy.

## Final local acceptance

- Mac release app and DMG built; deep strict signature, dSYM UUID, dylib closure, resource hashes and symlink checks pass (mac-package.json, dmg-validation.json). Packaged app launches from a neutral directory and runs the 1,500-tick fixture (mac-portable-launch.log, mac-headless.log).
- Executable: 26,822,808 -> 17,420,848 bytes after stripping. App logical size 74,912,110; DMG 57,968,678 bytes. These are final artifacts, not a controlled full-app before/after comparison.
- Two native image cases and two software graphics/runtime-pack cases pass (native-image-final.xml, graphics-tests.xml).
- Broad native unit run: 88 groups pass, one existing farming timing threshold fails; final isolated repeat 125,127 us against 100,000 us. Code for farming is unchanged (native-unit.xml, native-timing-final.xml). Do not claim all native tests pass.
- Chromium: exact WebP decoder/override test and six menu/cursor/team-color rendering tests pass (browser-image-test.log, browser-rendering-tests.log). First attempts before app build completion were discarded. A parallel gzip run collided in shared Playwright trace output; the isolated compressed-host WebGL retry passes (browser-gzip-webgl-repeat.log), as did the compressed-host software case.
- Browser package identity/gzip bytes and MIME types verified through Caddy (browser-package-http.json). Payload: 57,254,685 bytes identity -> 36,692,359 gzip, a 35.9% decrease (browser-package-size.json). This is HTTP compression of the new build, not a total old/new browser release comparison.
- Workflow/package YAML parses; Caddy configuration adapts; git diff whitespace checks pass.

Windows/mobile/Linux/Flatpak full builds still require their platform CI; none is claimed verified locally. No publishing or deployment performed. Source artwork remains original. No simulation, save or replay-format changes made.
