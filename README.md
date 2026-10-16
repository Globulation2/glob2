# PR 1194 correctness evidence

Download the [test/review evidence](ready-f56cbc8f6-tests.zip), [macOS mid-game evidence](ready-f56cbc8f6-mac-midgame.zip) and [Linux mid-game evidence](ready-f56cbc8f6-linux-midgame.zip). Archive sizes and SHA-256 hashes are in [archive-sha256.json](archive-sha256.json). Browse the [independent review](independent-review.md), [mid-game comparison](midgame-comparison.json), [publication comparison](publication-comparison.json), and [Windows checkout reproduction](windows-gradient-bundle/README.md) directly. Each archive has a per-file SHA-256 manifest.

Tested revision: `f56cbc8f624dd35c2066cce1aad51d0d9ada25cf`.
Integration base: `987c953698bb3f0bfd1078184773075ba3313091`.
The feature includes master's intrusive populations, material-demand changes and completed-world observation boundary. No source edits followed the final builds. The feature working tree is clean except for the user's unrelated `.playwright-mcp/` directory, excluded from manifests and evidence.

## Review and scope

Independent subagent review found no remaining concrete production blocker. `ready-review.md` describes ownership, invariants, resolved defects and limits. Review and continuation tests led to fixes for tick-zero access metadata, queued/admitted text-save section collisions, supplier pickup-penalty reachability, consumer supply-policy identity, and retained private air-choice allocations. Documentation describes the current ownership boundaries and exceptions.

Owner adapters enroll requests and reserve deterministic jobs. Existing pipelines capture immutable primitives, workers build complete private fields, and the owner publishes at fixed logical boundaries. Consumers handle Pending and perform current local movement checks. Immediate air A*, hazardous-terrain escape, explicit reference/editor builders and serial execution remain documented exceptions. The 64 MiB air reservation covers runtime-owned output generations; it is not a total memory ceiling or a budget for arbitrarily retained external leases.

Format 168 and SIM_REVISION 66 have regenerated committed goldens. Replay acceptance advances to 168; the durable full-save floor remains 58.

## Environments and commands

- macOS 26.6.2 ARM64, Apple Clang 21 / SDK 27, Python 3.14.7. Build: `PYTHONPATH="$PWD" python3 tools/dev_build.py unit-tests engine-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2 -j3`.
- Ubuntu 22.04 ARM64 in the owned `glob2-delayed-gradients` Docker container, GCC 11.4, Python 3.10.12. Build: `GLOB2_SDL3_PREFIX=/opt/glob2-sdl GLOB2_RECORDING_PREFIX=/root/.local/share/glob2/development/dependencies/build-bd81131fa4ce140bbfcb78e6/prefix python3 tools/dev_build.py engine-tests unit-tests build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 -j1`.
- Both use the repository's fast development configuration, `dev_fast=1`, `-O0 -g1`, all supported native features. Dependency paths, actual compiler versions, build logs, binary hashes and compiled provenance are retained in `ready-final-provenance/`. The mid-game source manifests match exactly across platforms: `60c2d5b403b91eae9597e6b25cc136840ef35723322fc753a9b92dd659cbea3b`.
- Each test group retains its exact command, selected inventory and genuine aggregate JUnit. Engine runs use `python3 -u test/run_tests.py --binary engine --build-dir ... --no-display -j 1 --timeout 600 --exclude-tag benchmark` with the filters recorded in its `command.txt`. Unit commands and logs are retained separately. Normal final verification never uses fixture recording/update flags.

## Final results

| Group | macOS | Linux |
| --- | --- | --- |
| Air/gradient pipeline and replay unit cases | 40 passed | 40 passed |
| Publication, invalidation and legacy population/save cases | 56 passed | 56 passed |
| Consumer gameplay, supply, hiring and continuation cases | 61 passed | Not repeated locally |
| Owner AI/world/resource/statistics pipelines | 108 passed | Not repeated locally |
| Four ordinary golden checks, MaterialDemand, TeamLimit and snapshot immutability | 22 passed | Not repeated locally |
| Turn-engine/network/replay, MarketsV2, ResourceFetchTarget, SavegameSafety | 29 passed; 1 display case skipped | Not repeated locally |

Counts describe separate groups, not distinct cases across all groups. Complete JUnit XML records zero failures/errors in successful final runs. An earlier integrated 102-case ground sweep also passed on `7bf507b10`; it is supplementary coverage, not misrepresented as a final-head run.

Two original Playground saves at ticks 12000 and 24000, each an eight-player Nicowar match on a 128x128 map, were continued for 256 ticks at participants 1, 2, 4 and 8 on each platform: 16 runs. Every corresponding per-tick `world.checksums` trace matches byte for byte across platforms and participant counts. Inputs, final saves, ordinary replays, commands, binary/source provenance, and comparisons are included under `ready-final-{mac,linux}-midgame/` and `ready-final-cross-platform.json`.

The dedicated publication comparison checks the exact 38 boundary/checksum trace inventory, byte-for-byte platform agreement, and all eight participant kernel-audit files per platform. Worker configurations require zero owner entries in the audited covered kernels and positive worker entries; serial configurations explicitly execute kernels on the owner while preserving publication trajectories. See `ready-final-publication-comparison.json` and the retained TSVs. This is a correctness audit, not an instruction-count or speedup measurement.

Coverage includes complete-field seed oracles, cold deduplication, FIFO pressure, hard identity/configuration supersession, soft invalidation without starvation, illegal local steps under stale guidance, Pending assignment/reservation retention, early completion invisibility, actual Game tick-zero capture and fixed deadline publication, malformed input rejection, binary/text checkpoints with simultaneous queued and admitted requests, released legacy boundaries, full continuation checksums and replay/network acceptance.

Documentation navigation (309 Markdown documents), private RNG ownership, simulation-version/golden contracts (including nine checker unit cases), and browser-test/Python syntax checks passed.

## Interruptions and limits

The shared host filled its disk during an initial final test attempt, stopping Docker and causing native tempfile/JUnit write failures. Those attempts are not counted as passing. Interrupted logs are retained separately; the native suites were restarted after space recovery and produced complete passing JUnit. Docker recovery preserved container data. Only this worktree's obsolete generated payloads and regenerable object files were removed; original input saves and current evidence were preserved.

Hosted run [38832889808](https://github.com/Globulation2/glob2/actions/runs/38832889808) requested Linux, Clang, Windows, Android and serial/threaded browser coverage. Windows failed before compilation because an existing master shader receipt hashes raw bytes after Git's CRLF conversion. All relevant shader/generator/Vulkan files are byte-identical to the integration base. `windows-gradient-bundle/` contains the full job log and actual Git checkout reproduction: LF passes, autocrlf=true reproduces the exact failure, candidate LF attributes pass, and tampered bytes remain rejected. This PR does not claim a successful Windows build or runtime check.

Windows/x86, browser runtime, Android/iOS and release runtime were not locally verified. Hosted checks may still be pending; consult the PR's current checks. The browser publication case was source-reviewed and syntax-checked but was not executed locally because Playwright was unavailable. The skipped native case requires a display. No release performance evaluation, Callgrind comparison or speedup claim is included, as requested. Cold-field waiting, finite bounded air routes and changed simulation trajectories need a maintainer gameplay-feel review.

Maintainer acceptance has not been asserted by this agent. The PR is prepared for review; it has not been merged.
