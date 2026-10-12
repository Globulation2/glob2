# Independent MCP implementation review and verification

For the final merge against current master, see [final integration verification](merge-verification.md).

The work is isolated in the managed `mcp-desktop-ui` worktree on `codex/mcp-desktop-ui`.
The base checkout is clean. No simulation revision, save format, or replay floor was changed.
The exact source snapshot and upstream revision are in [review-source-manifest.json](review-source-manifest.json).
Build flags, host and compiler are in [review-environment.json](review-environment.json).

Three independent reviewers iterated on protocol/transport, UI/input/capture, and quality/settings/documentation.
Their findings and resolved follow-up reviews are in [protocol review](review-protocol.md),
[UI review](review-ui.md), and [quality review](review-quality.md).

## Changes driven by review

- Separated the MCP settings implementation, localized listener/controller states, normalized source style,
  and documented tool registration and shared buffered-consumer contracts.
- Hardened metadata, JSON-RPC IDs, duplicate HTTP headers, MIME/Accept validation, handler registration,
  exact output schemas, and execution-error delivery after buffered cancellation.
- Removed SDL scancode-name lookup from worker threads and preserved SDL keycode option semantics.
- Added actual-consumer receipts for gameplay and editor buffering. Queued input no longer reports success
  before dispatch; cancellation erases exact remote ranges while preserving human events and UTF-8 storage.
- Corrected viewport/native-dialog rechecks, pending-input barriers, delayed hold/drag clocks, hidden
  cleanup, asynchronous listener retirement, and staged-release completion metadata.
- Added regressions for the discovered failures. Both UI and independent protocol/quality reviewers
  approved the final acknowledgement change.

## Verification

Final desktop production and focused test build: PASS. Exact build/link commands are retained in
[focused-build-command.json](focused-build-command.json), [focused-link-command.json](focused-link-command.json),
and [final-build.log](final-build.log). Source was frozen before final core and broad test runs.
[Tested binary hashes](review-binary-manifest.json) are retained too.

Final core run: 22 PASS, one touch-case timeout under the default 300-second limit;
all fourteen MCP cases, four ScreenExecution cases and four smaller touch cases passed.
The unchanged large touch case then PASSED alone in 225.1 seconds with a 600-second limit.
[Core log](review-final-core-tests.log), [core JUnit](review-final-core-junit.xml),
[touch rerun](review-touch-retry.log), [touch JUnit](review-touch-retry-junit.xml).

Final broad run: 109 PASS, one hostile-order fuzz-case timeout under its default 120-second limit.
Settings, capabilities, editor flows/action coverage/terrain paint, GUI interaction/order coverage,
software/portable rendering, resize, save safety, recording, all ordered renderer transitions,
and the other seventeen TurnEngine cases passed.
[Broad log](review-broad-tests.log), [broad JUnit](review-broad-junit.xml). The unchanged hostile-order
case subsequently PASSED alone in 122.9 seconds with a 600-second limit;
[rerun](review-turn-retry.log), [JUnit](review-turn-retry-junit.xml).

The reconciled core/broad/unchanged-engine inventory has 148 passing cases and the one
pre-existing replay-floor failure, with no unresolved timeout.
[Case-by-case evidence](review-reconciled-results.json) records the run supporting each outcome.
The earlier EngineSession cases reuse the unchanged Engine implementation and shared receipt APIs;
subsequent changes only corrected MCP service completion metadata.

Live official SDK client: both 2026-07-28 and 2025-11-25 PASS for discovery, all twelve tools exposed,
inspection, lease acquisition/release, modifier chords, release_inputs, inline PNG and stale-coordinate
execution failures. [SDK results](review-final-sdk-results.log), [auto PNG](sdk-auto.png), [legacy PNG](sdk-legacy.png).
The final live run used the final rebuilt binary after retiring prior disposable test processes.

Match verification: PASS; all 842 checksum rows compare byte-for-byte with
`test/fixtures/multiplayer/FourSquares1.verify-trace.txt`. [Verifier log](review-golden-verify.log),
[checksums](review-golden-verify/checksums.txt), [verdict](review-golden-verify/verdict.json).

Dedicated relay build: PASS. `nm -C` contains zero MCP symbols; [build](review-relay-build.log),
[exclusion result](review-relay-exclusion.json).
Browser exclusion: fourteen affected sources PASS Emscripten syntax compilation;
[results](review-browser-result.log), [compiler log](browser-compile.log), [command script](check-browser.py).
Windows Tools/Protocol/HttpServer syntax checks PASS under MinGW;
[coverage limits](windows-header-check/coverage.json) and windows-*.log retain details.
CI-policy unit checks: 24 PASS in [review-ci-policy.log](review-ci-policy.log).
Translation audit: all fourteen new English labels present,32 locales explicitly pending,
no new structural errors; [audit](review-translation-audit.json). Existing missing GPU-compute labels
and English-fallback audit failures predate this change.

Live final editor workflow: scaled-menu navigation, timed water painting, UTF-8 `Revue MCP é` filename,
Ctrl+A and save PASS; [drag log](review-final-drag.log), [painted frame](review-final-painted-map.png),
[text log](review-final-text.log), [UTF-8 frame](review-final-utf8-text.png),
[saved map](live-profile/maps/Revue_MCP_é.map.gz).

## Baseline failure and verification limits

The earlier affected EngineSession/ScreenExecution/MCP run had 31 passing cases and two failures.
The MCP pending-input completion failure was fixed and is covered by the final regression run.
The other failure is the retained-version-152 replay test: unchanged HEAD already requires replay
version 154, while that unchanged test expects version 152 to load. This is an existing mismatch,
not grounds to lower the compatibility floor. [Prior core results](review-core-tests.log),
[JUnit](review-core-junit.xml) preserve both outcomes. The unchanged baseline contract is retained in
[baseline-replay-contract.txt](baseline-replay-contract.txt).

A broad run was interrupted when its binary was relinked while new processes were being launched;
[interruption evidence](review-broad-interrupted.log). The replacement run uses the stable final binary.
A screenshot during expensive generated-map creation returned the documented ten-second deadline tool
failure; [live capture failure](review-live-ui-4.log). Input dispatch never promises generation completion.

Full Windows/macOS game builds and runtime, Android/iOS builds, browser link/runtime, native file-picker
interaction, device-loss injection and cross-platform checksum comparisons were unavailable here.
Windows checks use portable installed headers and host OpenSSL configuration; they are syntax-only.
Renderer capture/recording/transition and scaled-input coverage is Linux runtime coverage.

Fetched upstream master before final validation. Its newer DrawableSurface change adds cache retirement
only for a separately owned offscreen pass; memory capture uses complete presentation frames and does
not change that destructor path. The newer splash sizing change is likewise separate from MCP input
or capture. No merge/rebase was performed for these unrelated advances. Actual integration against that
newer master snapshot is not claimed. [Upstream changed-file inventory](upstream-changes.txt).

Evidence remains in ignored artifacts; durable guidance is in docs/development/mcp-ui-server.md
and docs/development/ui-framework.md. No generated images, logs or review narratives are committed.
