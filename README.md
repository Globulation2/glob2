# JavaScript AI library: reviewed implementation

Final source: `aa8d4e3c6a03feb13faee38f872201d44ec3b5e3`.
Integrated base: `723be2c4f2056c9c0932d0fdc1abd4ba7ec3bd2c`.
Master fetched before final verification: `90dac517f` (its additional Colony resume-control change does not touch the AI library).

Linux x86_64, GCC 15.2.0, Node 22.22.1, TypeScript 6.0.3, PostgreSQL 16.15. Release client, SDL3 prefix and fingerprint-checked recording dependencies are recorded in revision.json and build logs. Production binary and both native harnesses were rebuilt after all native changes. Later commits changed CSS, browser assertions, TypeScript test fixtures, and native whitespace only; native production inputs used by the final tests are unchanged.

## Independent reviews and improvements

Three sub-agents reviewed backend correctness/security, native code/storage/runtime, and UI/UX. Two rounds of review and improvements concluded with no remaining blockers in those scopes. The parent reviewed the combined changes and ran final integration verification.

- Social actions preserve the selected release and AI, including delayed responses; per-AI serialization avoids stale counters.
- Controller rejection diagnostics distinguish invalid decisions from valid construction later destroyed during ordinary play. Diagnostic flags are excluded from saved state/checksums.
- Native catalogue panes scroll independently; 24-result desktop and compact fixtures exercise selection, authors/tags, compatible older versions, installation and failures.
- Website filters, pagination, view and focus survive detail navigation and Back. Bounded URL restoration still permits browsing the whole catalogue via cursor segments.
- Edit/report forms restore keyboard focus. The introduction is concise, with permanent authoring details. Mobile file-picker overflow found during visual review is fixed.
- Abandoned pending validation jobs no longer retain private blobs indefinitely. Active leases, published evidence and in-flight results are preserved. Failed published validations retry even after old jobs are collected.
- Script-authored Bubblewrap text and invalid UTF-8 are classified correctly.
- Batched catalogue projections replace per-card queries; browse URL handling and maintenance cleanup have dedicated helpers; migration and touched native code were cleaned up.

## Final verification

- **739 headless unit cases passed**, 14 display cases omitted from this broad run. **98 focused native cases passed**, including display presentation, JavaScript save/replay/continuation/worker-count regressions, source-hash/provenance rollback, settings, music, and protocol parsing. See review/review-unit-final.xml and review/review-native-final.xml.
- **124 platform cases passed** across two focused runs (95 + 29). Five existing opt-in map-generator binary cases were skipped. Coverage includes publication, ownership/privacy, immutable bytes, per-version statistics, social idempotency, validation lifecycle, worker leases/retries, account lifecycle, schema/protocol contracts, music integration and web state. Logs: review-platform-final.log and review-job-integration.log.
- Desktop and Pixel 7 Chromium flows passed with **normal and reduced motion**. Axe checks cover catalogue, detail, editing, reporting and publishing. Browser Back, filter/focus restoration, older release preservation after favouriting, upload gating and file-picker containment are exercised. Screenshots, normal-motion videos and contact sheets are included.
- The UI reviewer manually inspected screenshots and normal-motion video frames: restrained hover movement, stable geometry, readable form transitions, and corrected native scroll layout. This is emulated-device review, not physical-device/full gameplay review.
- Real isolated jobs passed all seven checks for **profiles 1 and 2** on both pinned maps. Unsupported profile, syntax, startup, missing callback, invalid state, exhausted budget and invalid order inputs failed with dependent checks skipped. See reports/ and review-isolated-final.log.
- Full TypeScript checks, ESLint for all 35 changed TypeScript files, strict translation structure audit, CI-policy tests and production website build passed. Existing large-chunk warnings remain.

## Reproduction

From the repository root with dependencies described in revision.json:

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX="$PWD/artifacts/ai-library/deps/recording" scons -j8 release=1 server=0 unit-tests engine-tests
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX="$PWD/artifacts/ai-library/deps/recording" scons -j8 release=1 server=0
python3 test/run_tests.py --filter 'Settings*/*' --filter 'JavaScript*/*' --filter 'OnlineResources/*' --filter 'PlatformProtocol/*' --filter 'Music*/*' --artifacts artifacts/ai-library/review-native-final --junit artifacts/ai-library/review-native-final.xml
python3 test/run_tests.py --binary unit --no-display --artifacts artifacts/ai-library/review-unit-final --junit artifacts/ai-library/review-unit-final.xml
python3 data/check_translations.py --strict
python3 -m unittest discover -s test/build_system -p 'test_ci*.py'
cd platform
npm run typecheck
npx vitest run apps/api/test/ais.test.ts apps/api/test/accountExport.test.ts apps/api/test/deletion.test.ts packages/db/test/schema.test.ts apps/engine-agent/test apps/web/test/ais.test.tsx apps/web/test/aiBrowse.test.ts packages/protocol/test
npx vitest run packages/core/test/engineJobs.test.ts apps/api/test/engine.test.ts apps/worker/test/reliability.test.ts apps/worker/test/worker.test.ts apps/api/test/music.test.ts
npx playwright test -c ../artifacts/ai-library/playwright.config.ts
npx playwright test -c ../artifacts/ai-library/playwright-review.config.ts
npm run --workspace @glob2/web build
```

Put validation-harness.mts at artifacts/ai-library/validate.mts, then run:

```sh
mkdir -p artifacts/ai-library/scratch
unshare --user --map-root-user --mount sh -c 'mount -t tmpfs -o size=1g tmpfs artifacts/ai-library/scratch && ENGINE_AI_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib npx --yes tsx artifacts/ai-library/validate.mts'
```

The scratch mount is private to that process namespace. Controller processes have separate network/user/PID/mount namespaces and no credentials. Current native saves, replays and per-tick traces are in review/native-scripting-evidence.tar.gz. The older top-level native archive and unprefixed logs preserve the first implementation's verification history; review-prefixed logs and review/ are authoritative for this review cycle.

## Limits and rollout

No Windows, macOS, Android, iOS or browser-WASM build/checksum comparison was available. No production rollout/container-policy verification, physical-device testing, or full manual playing-feel assessment was performed. Simulation rules, save layout and network/replay acceptance are unchanged; SIM_REVISION is unchanged. New native text uses tracked English fallbacks pending translation.

Deploy migration 0040, the worker and an engine agent whose isolation probe passes before publishing becomes available. A host that disallows nested namespaces fails closed; there is no unsandboxed fallback. Passed compatibility checks are not a safety certification. Multiplayer custom AIs and tournaments remain out of scope.
