# AI Map Studio review evidence

Current integration is recorded in `integration-verification.json`, `artifacts/merge-native-check.log` (412 passed, no skips), `artifacts/merge-browser.log`, and `artifacts/studio-native/integration-delivery/`. Native delivery uses the rebuilt engine, real HTTP leases and worker-role result application. `artifacts/studio-native/integration-shapes/` repeats all 63 geometry cases against this engine. Current screenshots are in `artifacts/studio-web/integration/`. The original evidence below remains historical, tied to its recorded engine version.

The feature source is on `codex/ai-map-studio`, stacked on the preserved Hive feature (#605). This branch keeps transient evidence separate from maintained documentation.

- `artifacts/studio-review/verification.json`: checks, seeds, engine hash and qualification limits.
- `artifacts/studio-final-check.log`: full lint, TypeScript and 340-test result, with native cases enabled.
- `artifacts/studio-web/`: desktop and phone screenshots; the browser test uses fixture responses for authoring while serving the real app.
- `artifacts/studio-native/delivery/initial.*` and `revision.*`: actual map bytes, rendered previews and private delivery receipts from the real native pipeline. An identity image-provider stub exercises mechanics; it does not establish AI image quality.
- `artifacts/studio-native/shapes/`: all 63 requested size/player combinations, native import reports, reloaded reports, maps and previews. The synthetic grass-only input corpus establishes geometry/import compatibility, not playable economy.
- `artifacts/studio-native/marchland/`: the native reference image and imported map used for the gameplay check.
- `artifacts/studio-native/ai-game/`: a 30,000-tick game with Nicowar, Cortex, Cabino and Maxima, including final save, structured result and gzip-compressed checksum stream. All four colonies survived and expanded. This one reference case does not qualify real provider outputs or competitive balance.

Reproduce from the feature checkout with Node 22.18+ and PostgreSQL 16 on the documented test port, the pinned native SDL dependencies, and Python with Pillow:

```sh
GLOB2_SDL3_PREFIX=/path/to/pinned/prefix scons release=1 server=0 -j8 build/darwin/client/release/src/glob2
cd platform
GLOB2_BINARY=build/darwin/client/release/src/glob2 MAP_PYTHON=/path/to/python GLOB2_EVIDENCE_DIR="$PWD/../artifacts/studio-native/delivery" npm run check
npm run build -w @glob2/web
SCREENSHOT_DIR=../../../artifacts/studio-web npm run e2e -w @glob2/web -- studio.spec.ts
cd ..
GLOB2_BINARY=build/darwin/client/release/src/glob2 GLOB2_NATIVE_EVIDENCE=artifacts/studio-native/shapes /path/to/python platform/packages/map-studio/test/test_pipeline.py NativeImportTests
/path/to/python platform/packages/map-studio/test/test_pipeline.py EnvelopeTests
```

Native output paths above are for macOS; use the matching binary path for another platform. Generation and sales remain disabled until the live provider, Stripe, container and gameplay release gates have been qualified.
