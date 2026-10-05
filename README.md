# Community music verification

Tested feature revision: **1ae26bd3d8218108c854c1992267161dfd1fc92f**. Integrated base: **0a3da6df8898850f29050ec240e919e8b4a5b1b3**. Fetched master **b58be00a245f801442ab330bf0e4b9e14dfa431b** adds only an unrelated openSUSE release package-name correction. PR #746 is mergeable against it.

Three sub-agents reviewed backend correctness/security, native playback/import, and UI/UX through two rounds. They independently rechecked the changes and reported no remaining blockers in their reviewed scopes. This is agent review, not physical-device or human listening sign-off.

## Results

| Coverage | Result | Evidence |
|---|---|---|
| Platform/API, schema upgrades, restricted roles, privacy/account deletion, job reliability and protocol | 71 tests passed | [log](logs/review-final-platform.log) |
| Frontend regression tests: stale searches, consumed audio clock, pause/seek, visibility/retry | 6 passed | [log](logs/review-ui-final-unit.log) |
| Processor/pipeline including actual encoder → native importer/player | 128 cases: 122 passed, 6 optional-backend skips | [log](logs/review-python-final.log) |
| Native ZIP/audio/UI and real downloaded set | 9 passed | [log](logs/review-final-native.log) |
| Existing Settings layout/persistence/software/OpenGL | 6 passed | [log](logs/review-final-settings.log) |
| Website real WASM in five profiles plus real upload → inspect → convert → audition → publish → like → ZIP | 6 passed; 4 intentional repeated-upload-profile skips | [log](logs/review-ui-final-browser.log) |
| Browser game import, persistence/reload, quota failure, export and retry | 6 passed across Chromium/Firefox/WebKit | [log](logs/review-final-browser-game.log) |
| Malformed ZIP offsets/sizes on actual wasm32 pointers | 12 rejected | [log](logs/review-zip-bounds.log), [probe](probes/review-zip-bounds.cpp) |
| CI selection/contracts and deployment contracts | 87 + 34 passed | [CI log](logs/review-ci-contracts.log), [deployment log](logs/review-deployment-contracts.log) |
| TypeScript, full platform ESLint/Prettier, website build | Passed | [typecheck](logs/review-final-typecheck.log), [lint](logs/review-platform-lint-final.log), [web build](logs/review-ui-final-build.log) |
| Linux release engine and serial/threaded browser game build | Passed | [native build](logs/review-final-engine-build.log), [browser build](logs/review-final-browser-build.log) |
| Final music-worker container build and isolated mastering/conversion | Passed with network disabled/read-only root and production process limits/environment | [build](logs/review-final-container-build.log), [conversion](logs/review-final-container.log) |
| 15-minute preview memory | Native process peak 5208 KiB; browser decoder heap 51,380,224 bytes (not whole-browser RSS) | [native](review-final-memory.json), [browser](logs/review-final-long-browser.log) |

The initial broad Python run lacked existing optional analysis/composition core dependencies; the final run installs the repository requirements and passes. A direct container diagnostic initially omitted the worker's OpenBLAS/OMP thread environment and exhausted the PID limit; the [failed diagnostic](logs/review-container-missing-thread-env.log) is retained. The successful rerun uses the exact thread limits already set by process.ts. No runtime code change was necessary.

## Review improvements

- Reject wrapped ZIP directory/header offsets before arithmetic, including 32-bit targets.
- Retain uploaded source bytes across transient storage/database/processor failures and ambiguous COMMIT acknowledgements; terminate exhausted retries durably.
- Enqueue processing atomically with its release state. Avoid deleting committed replacement files or active draft sources.
- Cancel stale web/native catalogue requests and bind pagination to the submitted query. Extract the web catalogue hook and name native layout controls.
- Preserve queued PCM through pause/resume, report the consumed audio clock, respect visibility during initialization, and reset tester controls after playback errors.
- Export the actual failed import source; disable competing actions during persistence; label waveform lanes.

## Visual and audio evidence

[Desktop web player](images/web-desktop-music-player-light.png), [mobile dark player](images/web-phone-music-player-dark.png), [native touch player](images/native-touch-detail.png), [native catalogue](images/native-desktop-library.png), [browser quota recovery](images/game-chromium-recovery.png).

[Native crossfade audition](audio/native-crossfades.opus): synthetic CC0 tones, each mood selected for six seconds, looping for 36 seconds. Generated through the real shared native mixer; [source](probes/review-audition.cpp). The three [Calm](audio/a1.opus), [Building](audio/a2.opus), [Combat](audio/a3.opus) files include portable metadata. [Actual website download used by browser import tests](web-release.zip). The [earlier web download](native-import-input.zip) is the input of the final native regression run.

## Reproduction and limits

See [commands](commands.json), [web commands](review-ui-final-commands.json), [toolchain/dependencies/build flags](toolchain.json), and [file hashes](checksums.json). This evidence branch contains only review artifacts and is not merged into the feature branch.

Unavailable coverage: physical Android/iOS, Windows native build/playback, actual audio-device changes/failures, human listening/play-feel review, full composed self-hosting stack and live deployment. Browser device profiles emulate viewport/input; they do not establish physical mobile audio behavior. Unit audio fixtures cover envelopes/alignment/seek/loop and three output sample rates. No simulation rules, save format, replay/network protocol or SIM_REVISION changed; no cross-platform simulation-checksum claim is made. Hosted cheap checks are not comprehensive engine verification.
