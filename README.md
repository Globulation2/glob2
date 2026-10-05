# JavaScript AI library verification

Source revision: `a3bad6216571cc8c6007a42275bef4c657ee49b9`.
Integrated base: `723be2c4f2056c9c0932d0fdc1abd4ba7ec3bd2c`. Integrated the current community music library and menu-background changes. The AI migration follows music as 0040.

Linux x86_64, GCC 15.2.0, Node 22.22.1, TypeScript 6.0.3, PostgreSQL 16.15.
Native release client uses the SDL3 prefix and fingerprint-checked recording dependencies shown in revision.json and the build logs. No simulation rules, save layout, replay acceptance, or network format changed. New controller diagnostics are observational and excluded from serialization/checksums.

## Integration refresh

The music merge added a SoundMixer dependency missing from the unit link list. Adding MusicStream.cpp repaired that harness; both test binaries were rebuilt, and the broad unit suite passed. Missing music translation placeholders were reconciled with the shared catalogue and marked pending, retaining English fallback.

## Results

- 98 focused native cases pass (native-final.xml): JavaScript runtime, numeric corpus, save compatibility, continuation, replay, worker-count equality, local storage rollback/provenance, platform parsing, and desktop/compact library presentation. The native harnesses were rebuilt after integration at the source revision above. The engine command binary was built at 1921cb6c3; subsequent changes only repair unit-harness linkage and a TypeScript type import, leaving production native inputs unchanged. A broader headless unit run also passed 739 cases; 14 display-only cases were skipped there, with Settings and AI presentation covered by the separate focused run.
- Focused platform suite: 106 passing tests; 5 existing opt-in map-generator binary tests skipped. AI publishing, concurrent retries, duplicate labels/source, substituted reports, privacy, social idempotency, immutable versions, exact downloads, cleanup, account export/deletion, schema migrations, worker dispatch/retry, and protocol fixtures are included. The web checklist suite has 3 passing cases included in the focused run.
- Desktop and Pixel 7 Chromium flows pass: catalogue, light/dark WCAG axe checks, older-version selection, favourites, gated publishing, and no horizontal overflow. Screenshot evidence is in screenshots/.
- Real isolated AI jobs: valid profiles 1 and 2 pass all seven checks on both pinned maps. Unsupported profiles, syntax errors, startup errors, missing callbacks, unsavable initial globals, execution-budget exhaustion and invalid orders fail with dependent checks skipped. Reports and the reproduction harness are included.
- Isolation preflight refuses unbounded host scratch. The production runner also probes the engine inside Bubblewrap and does not advertise validate-ai on failure.
- TypeScript checks, focused ESLint, strict translation structure audit and production web build pass. The build retains existing large-chunk warnings.

## Reproduction

From the repository root, with the same dependencies:

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX="$PWD/artifacts/ai-library/deps/recording" scons -j8 release=1 server=0 unit-tests engine-tests
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX="$PWD/artifacts/ai-library/deps/recording" scons -j8 release=1 server=0
python3 test/run_tests.py --filter 'Settings*/*' --filter 'JavaScript*/*' --filter 'OnlineResources/*' --filter 'PlatformProtocol/*' --filter 'Music*/*' --artifacts artifacts/ai-library/native-final --junit artifacts/ai-library/native-final.xml
python3 data/check_translations.py --strict
cd platform
npm run typecheck
npx vitest run apps/api/test/ais.test.ts apps/api/test/accountExport.test.ts apps/api/test/deletion.test.ts packages/db/test/schema.test.ts apps/engine-agent/test apps/web/test/ais.test.tsx packages/protocol/test
npx playwright test -c ../artifacts/ai-library/playwright.config.ts
npm run --workspace @glob2/web build
```

The browser configuration starts Vite on 4287 and uses the committed API-mocked AI flow, with Desktop Chrome (1280×860) and Pixel 7 projects and reduced motion. API/database tests separately exercise real PostgreSQL and HTTP.

For the isolated reproduction, put validation-harness.mts at artifacts/ai-library/validate.mts and run:

```sh
mkdir -p artifacts/ai-library/scratch
unshare --user --map-root-user --mount sh -c 'mount -t tmpfs -o size=1g tmpfs artifacts/ai-library/scratch && ENGINE_AI_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib npx --yes tsx artifacts/ai-library/validate.mts'
```

The temporary mount is private to that process namespace. The harness runs the real production validator; the controller child has a separate network/user/PID/mount namespace and no credentials. Native saved games, replays and per-tick traces are in native-saves-replays-traces.tar.gz.

## Coverage limits and rollout

No Windows, macOS, Android, iOS or browser-WASM native build/checksum comparison was available. No production rollout or hosted expensive CI was requested. Visual review covered the captured desktop/compact layouts and both themes; full manual playing feel and normal-motion animation assessment remain review work. New native strings have English fallback entries and are tracked as pending translations.

Deploy migration 0040, the worker and a namespace-capable engine agent before enabling publishing. The current Compose host policy may prevent nested namespaces; the startup probe fails closed, and there is no privileged/unsandboxed fallback. Local execution keeps existing JavaScript trust limits. Passed compatibility checks are not a safety certification. Tournament and multiplayer custom-AI behaviour remain out of scope.
