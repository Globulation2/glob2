# Browser platform handoff — 2026-09-08

## Resume here

**Scope changed September 8:** the user requested reducing the remaining
multiplayer work. Finish existing YOG login/lobbies/rooms and browser/native
cross-play, including compatibility, safe handling, complete-match tests and
gateway setup documentation. Defer guests/invitations, account modernization,
checkpoint/reconnect recovery, host migration and production hosting/operations.
Refresh/disconnect can end participation. See [delivery scope](implementation.md).
Older original-plan checklists below are historical; do not restart deferred
features merely because an automatic goal reminder repeats the original plan.

Latest cleanup: CI run `34302157222` exposed a server-only link failure on both
Linux versions and Windows. An unused `Game.h` include in `MapHeader.cpp` pulled
script prototypes into the headless parser; it is removed. Local server/router/web
builds, nine browser map-load/cross-play cases and seven TLS tests pass. CI now
builds servers earlier and cancels superseded development-branch runs. Confirm
the next hosted result before declaring the build matrix green.

New browser profiles now default to Mute, alongside disabled clouds. Existing
saved settings override both defaults; desktop defaults are unchanged. Players
can enable sound through the existing Settings checkbox.

Map chooser failures now remain within the chooser instead of entering a blocking
message box. Missing/corrupt files clear the previous selection and preview, and
thumbnail allocations are released on exceptions. The unused legacy fertility
dialog has been removed. In-game load/replay continuation now finalizes through
`Engine::finishSessionForHost()` and transfers the existing engine to a cooperative
`GameLoadScreen` child. Successful loading returns it to `GameSessionScreen`;
failure uses a scheduled notice, and cancellation/failure return to the retained
parent setup screen. Native synchronous hosts retain an explicit adapter.
The browser reload regression also exposed script-GC lifetime bugs, now fixed
with per-interpreter method tables and complete live-reference marking; see
[script lifetimes](adr-007-script-lifetimes.md). The browser now builds without
Asyncify; legacy blocking waits explicitly reject browser use. See the latest
status entry for callback-only runtime qualification: 204 software and 120
WebGL2 cases pass across three engines, plus native/session and visibility checks.
The Linux real-window launcher now reports early exits and explicitly configures
its test-only sandbox/software GPU; confirm the next hosted run.

The latest runtime change moves YOG login/registration, lobby tab ownership,
map selection, join progress, map transfer navigation and error notices onto
the shared screen stack. Login transitions are deferred until network listener
dispatch returns. The native regression also covers destroying a completed tab,
including an empty tab. See the latest status section for validation.
The following change schedules YOG match loading/execution and settings too;
its validation is recorded in the latest status section. LAN hosting/joining now
use the same scheduled screens too, including cancellation and timeout handling.
The multiplayer tab no longer has a blocking fallback, and the unused blocking
LAN bring-up helper has been removed. The headless simulation driver retains its
explicit native host loop. Interactive browser flows use scheduled screens. Do not conflate
these runtime changes with completed reconnect, identity or protocol work.

Latest follow-up: browser reload/address-bar shortcuts pass automated checks
with both renderers and actual Safari. The user uses **Colemak**: this Mac's UI
automation physical S/U keys emit logical r/l, whereas its physical R/L emit
p/i. Do not interpret an automation `super+r` attempt as a delivered Command-R.
Windows CI passes. Linux exposed stale standalone harness dependencies, now
fixed with all 20 executables passing locally (170 CppUnit cases). The hosted
coexistence failure also reproduced locally on a cold configuration: SCons
only discovered the generated header on the second build. Registering it as a
generated target fixes the focused cold-build regression and local full
coexistence check. Hosted native-first and web-first jobs now pass, along with
Windows and both Linux jobs. Run 34292287930 completed: concurrent coexistence
passed, but browser qualification failed in Linux Firefox (three WebGL/context
tests fell back to software, shutdown reported rejected audio-resume promises,
and post-reload audio stayed suspended). The newer LAN runs are still pending.
The shell now handles its own audio resume/close race; SDL's pinned audio backend
also resumes suspended contexts internally, so a hosted pass is still required.
Follow-up reproduces both problems outside the game in the pinned Playwright
1.63.0 Linux container: headless Firefox cannot create WebGL2; Xvfb fixes that,
and a PulseAudio null sink lets audio resume. All five previously failing game
scenarios pass locally on Linux with this setup. CI now runs Firefox headed
under Xvfb with the silent audio service; hosted completion is still required.
See the latest dated status
sections rather than treating the older warm-build passes as cold-build proof.

The user resumed work after the subscription handoff. The first follow-up
verified the final click adapter in real-window Chromium with both renderers
and in software input regressions across three engines. Actual Safari 26.6.2
also passed a manual gameplay/save/reload/resize smoke check. See the latest
validation section in [status](status.md) and [Safari evidence](safari-smoke.md).

The closeout implementation was **`15986f936`**; upstream integration is
**`ce7e55d2d`**. Follow-up keyboard and CI changes are recorded in the latest
section of [status](status.md). Draft PR:
<https://github.com/Globulation2/glob2/pull/203>.

The user has played single-player successfully. They want completion and useful
chunks of work, not another architecture redesign or a misleading list that
makes implemented features sound absent. Continue by closing concrete defects
and qualifying the existing implementation. Keep multiplayer's larger remaining
scope distinct from single-player readiness.

## Checkouts and publishing

On the existing Mac:

- Implementation: `/Users/bradley/glob2-browser-clean`, `codex/browser-clean`.
- PR publishing: `/Users/bradley/glob2-browser-experiment`,
  `codex/browser-experiment` (the remote PR branch).
- `/Users/bradley/glob2` contains unrelated AI/Maxima work. **Do not change or
  reset it.** Other worktrees and their servers also belong to other tasks.
- Use `DEVELOPER_DIR=/Library/Developer/CommandLineTools` for native builds,
  git and gh; the default Xcode installation has an unaccepted license. Do not
  accept it or change the global developer directory.
- The SDK and `browser/node_modules` in the clean checkout are ignored symlinks
  into the publishing checkout. Preserve them; fresh machines can use setup below.
- Port **18770** serves `build/emscripten/client/release` from the clean checkout.
  Last observed server PID: **45654**. Check the current process before reusing or
  stopping it. Ports 8765/8770 and other tasks' servers are not ours to replace.

Before editing, check the branch and worktree status. Publishing was authorized:
verify the publishing checkout is clean, fast-forward it from
`codex/browser-clean`, then push `origin codex/browser-experiment`. Do not merge
the PR or force-push. Keep the PR draft until the outstanding gates are met.
No builds or tests from this task should remain running after this handoff.

## What already works

- Isolated native/Emscripten SCons builds with shared source manifests.
- Full-page canvas, live viewport resolution, software fallback and actual
  opt-in WebGL2 through the shared desktop GPU renderer.
- Campaigns/tutorials, custom games, AI, map editing, saves and replays.
- Cooperative loading/generation, screen-stack navigation, resize and context
  restoration; single-player hidden-tab pause has earlier passing evidence.
- Atomic saves/imports, durable persistence with visible failure/retry,
  save/map/replay import/export and campaign-progress backups/merge.
- Durable settings/keyboard saves. New browser profiles default to clouds off;
  saved choices override the default and desktop defaults are unchanged.
- Browser WebSockets, native TCP/WSS, fixed-backend gateway and initial YOG
  cross-play. Exact protocol-29 admission is checked before credentials.
- A maintained Playwright suite, native harnesses and CI checks.

This is still an experimental platform, not a supported stable release.
`?renderer=webgl2` selects GPU rendering; software is still the default pending
performance and actual-browser qualification.

## This session's changes

1. **Orderly Quit:** the shared application destroys gameplay screens, checks
   final preference writing, then keeps a shutdown screen alive until storage
   completes. Failure offers Retry or explicit Quit without saving. Repeated
   window-close events do not bypass the pending write. Abrupt tab/process
   termination cannot wait for asynchronous storage.
2. **Campaign authoring:** creation/editing now waits for durable storage after
   atomic writing. Failure retains the editor and allows retry; pending writes
   hide editing/navigation controls. The blocking save-error message box is gone.
   Cancel after failure does not promise rollback of already written local bytes.
3. **Click-coordinate fix:** the real-window test reproduced a missed map click.
   SDL 2.32.8's Emscripten button callback uses the last mouse-motion position.
   The browser shell now synchronizes absolute motion from button coordinates
   before SDL receives the button. Relative pointer lock is excluded.
4. Added storage/input regressions, native shutdown coverage, CI selections and
   reviewed screenshots. Reworked the status overview to distinguish delivered
   features from uncompleted qualification gates.

## Exact validation boundary

All logs below are on the existing Mac under `/tmp`; the maintained tests and
summaries are committed. Do not claim every test ran on the final input adapter.

| Scope | Result | Log |
|---|---|---|
| Native desktop and release Wasm, persistence implementation | Pass | `glob2-closeout-desktop.log`, `glob2-closeout-web-build.log` |
| Native session harness, including final persistence | Pass | `glob2-closeout-session.log` |
| Browser unit tests / build-system tests | 11 / 9 pass | `glob2-closeout-unit.log`, `glob2-closeout-build-tests.log` |
| WebGL-selected gameplay, viewport and rendering, three engines | 69 pass, 5.5m | `glob2-closeout-single-player.log` |
| WebGL-selected settings/shutdown/campaign-authoring, three engines | 27 pass, 2.4m | `glob2-closeout-storage-tests.log` |
| Software shutdown/campaign-authoring, Chromium | 5 pass | `glob2-closeout-software.log` |
| Real-window WebGL visibility, before input adapter | Failed at map selection, before visibility assertions | `glob2-closeout-visibility-webgl.log` |
| Injected missing-motion regression, before adapter | Fails in Chromium as intended | `glob2-input-before.log` |
| Release Wasm with final input adapter | Pass | `glob2-input-web-build.log` |
| Missing-motion regression with final adapter | 3 pass across Chromium/Firefox/WebKit, 51.6s | `glob2-input-after.log` |

The real-window failure capture and DOM input log live under
`build/browser-closeout-visibility-webgl/`. They show correct DOM click positions,
a focused visible page, and no selected map. The new test suppresses trusted
motion delivery while retaining actual button input; it starts a match, resizes
and quits successfully after the fix. These were the original handoff gaps. The resumed real-window checks now pass
in both renderers, and software input passes in all three engines. Consult the
latest status section for the resumed broad regression results. These focused
passes do not establish that all possible focus/input races are resolved.

Earlier evidence remains in [status](status.md): 114 selected WebGL scenarios
at `6d1d8bbae`, and scoped multiplayer matrices from earlier commits. Multiplayer
was not rerun in this session; simulation and protocol code did not change.

## Next work

1. Verify the latest hosted CI. Windows, both Linux native jobs and both sequential
   build orders pass on recent runs; concurrent browser qualification still needs
   a completed green result. Do not equate local passes with a hosted pass.
2. Finish release qualification: actual Safari/Edge matrix, controlled renderer
   performance, and the remaining persistence/input audit. Safari 26.6.2 has a
   manual smoke pass; see `safari-smoke.md` for its limits. Browser scheduling is
   callback-only and no longer requires Asyncify.
3. Close the existing YOG gates: complete browser/browser and browser/native
   matches, compatibility/safe message handling, native-platform determinism and
   gateway setup documentation. Guests, invitations, account modernization,
   recovery and production hosting remain deferred under the amended scope.
4. Get maintainer feedback and finish review qualification. Keep the PR draft
   while the supported-release gates above remain unresolved.

Do not turn these qualification items into claims that WebGL, persistence,
cooperative loading or cross-play still need to be implemented from scratch.
See [storage](storage.md), [viewport/input](viewport.md), [protocol](protocol.md)
and [delivery contracts](implementation.md) for precise limitations.

## Commands

Run from the clean checkout. On a fresh machine, install Python/SCons first,
run `python3 browser/setup.py`, and use `npm ci` plus `npx playwright install`
inside `browser`. SDK pin: Emscripten **4.0.15**.

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools scons target=web release=1 -j4
DEVELOPER_DIR=/Library/Developer/CommandLineTools scons release=1 -j4 session-test
python3 test/run-engine-session-test.py
python3 -m unittest discover -s tests/build_system
```

If port 18770 is not already serving this checkout:

```sh
python3 -m http.server 18770 --bind 127.0.0.1 --directory build/emscripten/client/release
```

Then inside `browser`:

```sh
node --test unit/*.test.js
GLOB2_TEST_URL=http://127.0.0.1:18770 GLOB2_TEST_RENDERER=webgl2 npx playwright test --config visibility.config.js
GLOB2_TEST_URL=http://127.0.0.1:18770 GLOB2_TEST_RENDERER=software npx playwright test --config visibility.config.js
GLOB2_TEST_URL=http://127.0.0.1:18770 GLOB2_TEST_RENDERER=software npx playwright test input.spec.js
GLOB2_TEST_URL=http://127.0.0.1:18770 GLOB2_TEST_RENDERER=webgl2 npx playwright test input.spec.js single-player.spec.js viewport.spec.js rendering.spec.js settings-storage.spec.js shutdown-storage.spec.js campaign-editor-storage.spec.js
```

Use unique `--output` directories when retaining multiple runs. Linux headed
visibility testing needs `xvfb-run -a`. Never rebuild the served Wasm artifacts
while Playwright is running. Never run simultaneous SCons builds for the same
target/configuration identity. Multiplayer suites share fixed backend ports and
must not run concurrently with each other.
