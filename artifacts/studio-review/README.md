# AI Map Studio review evidence

Current integration is recorded in `integration-verification.json`: Studio merged as `3092bdedab398157c7549c034637b97fa2d6daca` on simulation minor 130 / network protocol 52. All 426 platform tests, 617 native unit cases and 439 native engine cases pass locally. All 63 supported Studio import/reload combinations pass, and the current golden match verifies with a byte-identical checksum trace. Native display cases and the unrelated procedural-generator matrix were excluded. Six sprite/font cases validate the small master CRLF fix included during squash integration. `native-delivery-receipt.json` summarizes the real leased HTTP delivery fixture without internal credentials. Current screenshots are in `artifacts/studio-web/merge-final/`. Hosted CI is pending and the preceding Windows LAN crash remains unqualified. Generation and sales are disabled. Earlier prototype evidence below remains historical, tied to its recorded engine version.

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
GLOB2_SDL3_PREFIX=/path/to/pinned/prefix scons release=1 role=client -j8 build/darwin/client/release/src/glob2
cd platform
GLOB2_BINARY=build/darwin/client/release/src/glob2 MAP_PYTHON=/path/to/python GLOB2_EVIDENCE_DIR="$PWD/../artifacts/studio-native/delivery" npm run check
npm run build -w @glob2/web
SCREENSHOT_DIR=../../../artifacts/studio-web npm run e2e -w @glob2/web -- studio.spec.ts
cd ..
GLOB2_BINARY=build/darwin/client/release/src/glob2 GLOB2_NATIVE_EVIDENCE=artifacts/studio-native/shapes /path/to/python platform/packages/map-studio/test/test_pipeline.py NativeImportTests
/path/to/python platform/packages/map-studio/test/test_pipeline.py EnvelopeTests
```

Native output paths above are for macOS; use the matching binary path for another platform. Generation and sales remain disabled until the live provider, Stripe, container and gameplay release gates have been qualified.
