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

## Current state

The implemented port is ready for final CI and maintainer review, not a claim of
stable platform qualification. Single-player, WebGL2, durable storage, callback
scheduling without Asyncify and existing YOG cross-play are implemented.
New browser profiles are muted with clouds disabled. Software rendering remains
the default; `?renderer=webgl2` selects the GPU renderer.

The final audit fixed the end-game Save Replay dialog: scheduled overlay,
atomic writes, durable persistence and export/retry. Tests cover resize, quota
failure, refresh and playback. Private interpreter headers now stay out of public
game headers, fixing GCC headless links. Complete-match tests also exposed and
fixed a headless client crash when resignation tried to draw a player-left message.

See the newest [status entry](status.md) for exact test evidence and the PR checks
for hosted results: <https://github.com/Globulation2/glob2/pull/203>.
Older entries in the ledger are historical, including superseded failures.

The user uses **Colemak**. On this Mac, UI automation physical S/U keys emit
logical r/l; physical R/L emit p/i. Do not change the keyboard layout or mistake
an automation Command-R attempt for a delivered browser reload.

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
the PR or force-push. Review readiness is separate from stable-release qualification.

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

## Remaining qualification

Finish the latest hosted CI run and resolve any failures before marking review
readiness. The current source changes have local native, Wasm, Linux headless,
replay persistence and complete-match regression evidence in [status](status.md).
Do not substitute local results for a completed hosted run.

Stable support still requires the full actual Safari/Edge version matrix,
controlled renderer performance measurements, broader native-platform determinism
and release qualification. Safari 26.6.2 has a scoped manual smoke pass; see
[safari smoke](safari-smoke.md). These limitations should remain visible in the PR,
but do not prevent maintainers from reviewing the experimental implementation.

Do not restart deferred multiplayer features or redesign working architecture.
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
