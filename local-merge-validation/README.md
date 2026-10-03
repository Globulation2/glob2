# Hive Mind local merge validation

The user explicitly approved merging PR #605 after relevant local tests pass while hosted CI is unreliable. This evidence records the local checks and their limits; it does not claim a successful hosted matrix.

Source revision: `ff19428d7` (integrates master `4da094543`, including recording, sprite packaging and farm-area changes).

## Completed checks

- Platform `npm ci` and `npm run check`: lint, formatting, type checks, 396 tests passed, 5 skipped.
- Build-system contracts with pinned asset-encoder Python: 286 tests run, 3 skipped, no failures.
- Strict translations: zero structural errors.
- Browser unit tests: 34 passed.

- Native release build passed (GCC 15.2, Linux). Broad no-display suite: 512 passed, 88 skipped; two initial failures and their reruns are documented below.
- Hive commander presentation and Settings software/OpenGL UI: 3 passed.
- Native replay traces generated successfully. All four 1500-tick serial/threaded browser traces match byte-for-byte; the 703-line committed match trace also matches.

## Local dependency and timeout corrections

The older local SDL SDK lacked the existing repository SDL_ttf kerning patch. Built a private shared SDL_ttf from the checksum-pinned, repository-patched 3.2.2 sources and reran all three FontKerning cases successfully using that library. The UI cases above also used that patched library. No source/test assertion was changed.

The catalog-wide map-generator test exceeded its 600-second wall-clock limit on the shared host. Its unchanged retry uses a 1800-second local timeout. The unchanged test passed in 959.5 seconds.

Native binary provenance records commit `18198f73807e3c84b4cb0ce9201754122758407a`; the subsequent merge at `ff19428d7` changes only the platform room test fixture. The platform checks ran after that merge.

## Browser checks

Chromium: 10 worker/replay/staged-asset integration tests passed. Firefox worker/native parity and forbidden-capability cases passed. WebKit initially could not launch due to missing Ubuntu shared libraries; a private dependency directory and launcher were prepared for its rerun. The unchanged WebKit worker test passed with those dependencies. No browser/test source was modified.

## Integration fixes

New AI telemetry presentation values are in a plain-data header, keeping simulation interfaces out of Scene. The boundary test checks that header as well as scene headers. A blank line at the join between Hive and custom-AI translations was removed to preserve key/value alignment. Neither fix changes simulation rules or save formats.

## Merge status

PR #605 was merged concurrently into master as `a23089cb38ae48a259b8ca97852e483d5324eea3`, using PR head `ff19428d76b319351fa41bdde65b2162da727a92`, while the final local checks were running. The master merge additionally contains #655 (CRLF handling for sprite-sheet indexes); that independent change is not part of this validated PR head. The merge monitor is paused.

## Limits

Local native validation uses Linux GCC and the existing pinned SDL3 SDK. The tests do not establish Windows/macOS compatibility. All 514 selected native cases passed across the initial run and documented corrective reruns; 88 display/benchmark cases were skipped by the no-display policy, with three relevant UI cases run separately. Earlier Windows CI exposed a LAN harness access violation after the environment/path fixes; current Windows validation remains incomplete. Earlier map-generator CI reported stale revision rows inherited from master, not a Hive generator change. These historical results are distinct from the focused local feature acceptance evidence.

Production feature and sales flags remain disabled. No deployment or production credentials are used. Real multiplayer playtesting, platform containment, payment end-to-end testing and dedicated credentials/prices remain release requirements.
