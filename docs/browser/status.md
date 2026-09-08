# Implementation status

Browser support remains experimental. This ledger distinguishes delivered
infrastructure from the supported-release acceptance criteria.

## Delivered in this branch

- In-game save/map/replay import and export use a browser file-selection
  adapter and a shared C++ validation/persistence job. Complete input is read
  before an atomic write; name collisions create numbered copies, preserving
  existing files. The chooser waits for durable persistence, offers export and
  retry on failure, and supports cancellation while validating. See
  [browser files](storage.md) for behavior, boundaries and remaining work.
- Map headers use checked binary reads, reject unsupported versions, negative or
  excessive team counts and invalid saved-game flags, and retain the previous
  header if parsing fails. Malformed player records now fail loading instead
  of invoking a constructor assertion. Validation is shared by desktop/browser.
- Shared SCons source manifests and native/web configuration isolation,
  including generated headers, compilation databases, objects, caches,
  signature databases, temporary directories, and build-directory locking.
- SDK revision selection and checksummed Emscripten ports, including Boost.
- Native desktop, headless lobby, headless router, WebSocket gateway, and
  browser build entry points. The headless router defaults to a local lobby;
  `GLOB2_YOG_HOST` selects another private lobby.
- Shared injectable byte transports: native TCP/WSS and browser WebSocket, with
  transport-independent framing, bounded queues, malformed-input rejection, and
  greetings queued during connection establishment. Browser YOG login now reaches
  a native server through the gateway in integration tests. Successful account
  login and lobby exit are covered; the browser uses YOG chat without the optional
  native IRC bridge. Passwords are redacted from authentication message formatting.
- Real-control multiplayer tests create, join, ready, and start YOG rooms,
  then compare browser/browser order checksums. A native headless client records
  250 ticks for comparison with browser checksums at command boundaries.
- A bounded fixed-backend gateway with origin checks, backpressure,
  health/metrics endpoints, and real TCP/WebSocket integration tests.
- A development Compose deployment with private lobby/router/gateway services,
  Caddy HTTPS/WSS routing, persistent volumes, and container health checks.
  The lobby supports explicit external-router mode; desktop/LAN keeps the
  embedded router default. Empty router pools refuse room creation safely.
- An explicit native/browser application-host boundary for transitional
  waits and diagnostics. No forced SDL delay macro or sleep inside rendering.
- Explicit screen begin/update/input/draw/finish phases, with a legacy host-loop
  wrapper and automated lifecycle/completion tests. An owning screen stack
  now drives campaign selection, custom setup/options, load/replay selection,
  single-player sessions, and end-game transitions with retained ownership.
  The outer application update API is shared by native polling and scheduled
  browser callbacks. The map editor also uses explicit input/update/draw phases
  and an owned save-before-quit prompt. Nested editor/network dialogs and loaders
  still need migration.
- Incremental engine session phases with host-supplied timing, separate drawing,
  and delay calculation. Gameplay accepts explicit input batches and tracks
  held input from events, clearing it on focus loss. Modal handling, application
  visibility, and loading remain transitional.
- Bounded, cancellable fertility jobs with staged results and an explicit commit.
  Editor overlay/save actions run them through a scheduled progress screen;
  cancellation preserves unsaved edits. Old-format cooperative loading also uses the job; file serialization and durable
  storage remain transitional.
- Explicit cooperative game/map loading jobs and a cancellable editor-entry
  loading screen with staged ownership and RNG restoration. Terrain chunks and
  team parsing yield; the shared 8-bit helper gradient sweeps can also yield; other expensive operations and loading
  callers still need migration and latency validation. Single-player startup
  (custom/save/replay/campaign) now owns the engine in a cancellable loading
  screen, including replay cleanup and error transitions. In-editor replacement
  retains the current map until a new one loads successfully; cancellation and
  failure preserve its unsaved edits.
- Explicit generation seeds and per-instance noise state, removing generation's
  libc RNG/time reseeding and shared-noise interference. All nine native generation
  fixtures match synchronous and scheduled execution after unrelated RNG/noise
  activity. Editor generation uses an owned cancellable preparation screen with
  RNG restoration and error transitions. Height-map noise, stamps, placement searches,
  and normalization now yield through nested jobs with owned temporary arrays and
  instance-local stamp state. Concrete-islands/isles distance floods, point spacing,
  weighted area expansion, player-land partitioning, point collection/filtering,
  resource filling, oval creation, and area scoring also use nested jobs.
  Upstream resource and area gradients remain lazily constructed.
  Building-specific gradients and other long terrain operations still need subdivision;
  cross-platform generation parity is not yet certified.
- Shared measured loading/generation slices with an injectable steady clock,
  a four-millisecond target, and a 64-checkpoint cap. Deterministic tests cover
  elapsed-time stopping, completion, oversized steps, and a frozen clock.
- Saved-team parsing yields while loading units, buildings, and their links.
  The preparation game owns partial teams throughout cancellation and failure.
- A maintained, dependency-locked Playwright suite with real input and
  read-only diagnostics, plus CI failure traces.
- A replay-stall fix: measure the waiting-player mask after local orders are
  inserted, so filtered replay null orders cannot leave a stale waiting flag.

- Frame-boundary software viewport resizing for scheduled single-player menus,
  matches and the editor, including camera-center tiles, minimap hit regions,
  centered overlays and an in-game minimum-size notice. See [the contract and
  remaining limitations](viewport.md).

- Browser visibility changes suspend scheduled screens, discard held gameplay/editor
  gestures, and reset the single-player timing baseline on return. Native hosts
  retain their visibility policy. A dedicated real-window Chromium test disables
  Playwright focus overrides and checks hidden/visible transitions, suspended
  ticks, resumption without catch-up, and working input afterward. This does not
  implement coordinated multiplayer suspension.

- In-game manual saves use the shared checked atomic-write helper instead of
  overwriting the destination directly. A write failure retains the prior file
  and keeps the save dialog open with an error. Native short-write/flush failure
  regressions and all three browser save/reload scenarios pass. This protects
  filesystem replacement; durable completion is covered by the persistence flow below.

- Browser persistence uses a serialized coordinator instead of the SDK queue
  that drops persistence errors. Generation-specific completion promises wait
  for their write callback, failures remain observable, and failed restore
  prevents later writes from replacing unrestored data. Four injected-adapter
  tests and all three browser save/reload tests pass. Completion/failure UI,
  quota fault injection and recovery/export are covered below.

- Manual in-game save dialogs retain an owned platform persistence operation,
  show a saving caption while pending, close only after success, and remain open
  with an error on failure. Native injected completion-state tests and browser
  save/reload tests across all three engines pass. Quota/error injection and
  export recovery are covered below; other legacy persistence callers remain.

- Six browser database-boundary fault tests pass across Chromium, Firefox and
  WebKit: aborted transactions and injected quota errors preserve the previous
  durable save (verified from another page), then retry persists the replacement
  across reload. The short failure caption is visually checked in
  `screenshots/save-persistence-failure.png`. These are injected faults, not a
  full browser-profile capacity or interrupted-upgrade qualification.

- Failed browser manual saves offer an in-game download of the retained file.
  Export reads through the shared filesystem interface, bounds allocation to
  64 MiB, and passes bytes to the browser host. All six abort/quota scenarios
  verify the downloaded filename and byte digest, preservation of the old
  durable save, and successful retry. Campaign-progress handling is covered below; explicit messaging for
  oversized simulation-file exports remains unfinished.

- Failed browser storage restoration opens an in-game explanation before the
  menu. Players may continue with persistence disabled and export manual saves,
  then reload to retry restoration. Three database-open failure scenarios pass
  across Chromium, Firefox and WebKit, including no write retries while restore
  is failed and a successful fresh restore. The notice screenshot is checked.

- The browser load-game chooser offers normal export of its selected save or
  replay through the shared bounded file-export helper. Three save/export/load
  scenarios pass across Chromium, Firefox and WebKit, verifying exact downloaded
  bytes. This earlier export coverage is extended by the import flow above.
  Campaign-progress support is covered below. Native build and session checks pass.

## Campaign-progress support

- Browser campaign/tutorial menus import and export versioned progress backups.
  Validation matches mission definitions and merges completion/unlock state
  without removing current progress. Legacy text saves remain compatible.
- Campaign saves use checked atomic replacement. Menus wait for browser
  persistence, expose retry and backup export on failure, and restore previous
  bytes (or remove a newly created file) when the user discards a failed save.
- Twelve campaign backup/persistence scenarios and nine existing campaign,
  tutorial and editor-menu scenarios pass across Chromium, Firefox and WebKit.
  Native client/Wasm builds and the expanded native save-safety harness pass.
  The format and tradeoffs are documented in [browser files](storage.md).
  Review screenshots: [restored progress](screenshots/campaign-progress-import.png)
  and [persistence failure with recovery controls](screenshots/campaign-persistence-failure.png).

## Import-flow verification (September 2026)

- Twelve import/export scenarios pass across Chromium, Firefox and WebKit:
  saved-game round trips and continuation, duplicate-name preservation, malformed
  input rejection, custom maps, complete replay command streams, and quota
  failure/export/retry. Save tests explicitly select and verify filenames; an
  initial test accidentally exported an autosave and was corrected.
- The existing 75 single-player, storage, viewport and rendering scenarios pass
  across the three engines. The first run passed 52 before the local HTTP server
  began returning empty replies; all 25 WebKit scenarios passed after restarting
  that server. Multiplayer was not rerun for this import change.
- Native client and release Wasm builds succeed. The native save-safety harness
  covers complete imports, malformed headers/player records/map offsets,
  cancellation, RNG and preference preservation, and persistence failure/retry.
  Eleven browser adapter unit tests and nine build-system tests pass.
- Review screenshots: [save import](screenshots/imported-save.png),
  [map import](screenshots/imported-map.png), and
  [persistence failure](screenshots/import-persistence-failure.png).

## Local validation

After isolating browser work and rebasing onto upstream `master` (`88934ecf`),
on macOS arm64 in September 2026:

- Desktop and release Wasm builds succeed from the same checkout.
- Nine build identity/architecture tests, seven native WSS tests, eight gateway
  tests, and shared framing/native TCP round-trip tests pass.
- Native session/loading/cancellation tests pass, including matching 50-tick
  checksums under regular callbacks, delayed callbacks, and the screen stack.
- Upstream save-safety and weighted-gradient tests pass. Binary-string tests
  now cover embedded zero bytes, which occur in persisted password hashes.
- All 42 single-player browser scenarios pass across Chromium, Firefox, and
  WebKit after fixing an empty-gradient refresh loop on the first loaded tick.
  The native save-safety harness also covers ticks before any lazy gradient
  has been requested. Loading/error translations use the required paired-line
  format so the progress screens display their messages.
- Both Compose deployment tests pass after the binary-string fix: trusted
  HTTPS/WSS, private routes, account persistence across recreation, router-loss
  refusal, and admission after restart.
- All 18 default multiplayer scenarios pass across Chromium, Firefox, and
  WebKit: protocol/login and lobby flows, browser/browser matches without AI
  and with Cortex, and browser/native 250-tick command-boundary checksum
  comparisons through both TCP and verified WSS. The native peer ran on macOS
  arm64; this is not the complete cross-platform per-tick release matrix.
  The six upstream AIs are retained unchanged. The optional all-AI suite still
  needs rerunning after isolation; no Maxima or tournament changes are included.
- `screenshots/native-cross-play.png` is refreshed from the post-rebase
  Chromium/native TCP test.

After the subsequent rebase onto `f50ab27ac`, desktop and Wasm builds, all 42
single-player scenarios, nine build-system checks, native session/resize tests,
and current/version-88/version-84 team-statistics save fixtures pass. All 15 viewport scenarios also pass across Chromium, Firefox and WebKit, with
reviewed screenshots in `screenshots/resized-game-menu.png` and
`screenshots/minimum-viewport.png`. Earlier
multiplayer and deployment results above have not yet been rerun at this revision.

These are focused regressions, not a complete campaign, AI, deterministic
cross-platform, or supported-browser certification matrix.

## Still required for the supported release

1. Complete dependency archive verification and reproducible release gates
   on all supported native toolchains.
2. Complete editor/network/modal transitions, resumable/cancellable loading,
   and removal of Asyncify from the scheduled browser host.
3. WebGL2 performance and remaining context-loss qualification, resize in remaining legacy flows,
   complete gesture/layout qualification, focus/visibility behavior, and browser interaction handling.
4. Transactional persistence with durable completion and failure states,
   quota handling, and validated import/export for all local data types.
5. TLS-only internet connection policy, native trust-store qualification, capability/simulation/data-hash negotiation,
   bounded protocol parsing, and deterministic browser/native cross-play.
6. YOG guests/accounts, invitations, room controls, password migration, and
   coordinated 120-second checkpoint-based recovery with fault injection.
7. Versioned self-hosting distribution, TLS deployment tests, backup/migration,
   draining/rollback, operations documentation, and performance/soak gates.

Short cross-play tests do not establish sustained determinism across native
platforms. Reconnect, complete renderer qualification, persistence coverage for
all data types, and stable browser support remain outstanding.

## Immediate delivery focus

The current milestone adds WebGL2 drawing, live resize, software fallback and
context restoration together. Next, finish the release gaps recorded in ADR 006
and the remaining multiplayer/self-hosting work. Single-player is already
playable; further refactoring must resolve a concrete release blocker. The
upgraded handshake, secure endpoint policy, identities/rooms and coordinated
recovery remain required, as do complete persistence and Asyncify removal.

## Gameplay evidence

Chromium during the browser/native integration match on macOS arm64. This
screenshot illustrates the full-page client; the automated checksum assertions,
not the image, establish the short cross-play result above.

![Browser client during native cross-play](screenshots/native-cross-play.png)


## WebGL2 renderer milestone

The browser now provides actual WebGL2 GPU drawing using `?renderer=webgl2`,
with software fallback when unavailable. Software remains the default because
initial GPU performance and full-suite qualification are unresolved. The existing
2D GPU renderer is shared with desktop through the pinned Emscripten compatibility
layer; see [ADR 006](adr-006-webgl2-rendering.md) for that delivery compromise and
its maintenance costs. Texture initialization and atlas normalization fixes also
keep the native non-rectangle texture path valid.

The rendering suite verifies real custom-game controls, drawing-buffer resize,
software selection, and two context-loss/restoration cycles without restarting
the match. All 93 default-suite scenarios pass in Chromium, Firefox and WebKit
on macOS arm64, including the dedicated opt-in WebGL cases. The 15 viewport
scenarios also pass with WebGL selected explicitly; one Chromium cold-start
timeout passed on a focused rerun. The startup allowance now accommodates cold
texture creation on headless software GPUs. The assertions remain unchanged.
Both native and Wasm clients build, all nine build-system tests pass, and the
native session harness passes. A fresh current-build WebGL run passes all 22
Chromium single-player, viewport and rendering scenarios, including the earlier
editor-load and startup-cancellation deadline failures, without changing their
deadlines. Complete cross-browser GPU qualification, controlled performance
baselines and actual Safari/Edge release testing remain open.

![WebGL2 match after viewport resize](screenshots/webgl2-match.png)
![Same running session after two graphics-context restorations](screenshots/webgl2-restored.png)

The settings/editor/confirmation context-restoration scenario also passes in
Chromium, Firefox and WebKit. It restores the real context three times and
verifies the retained controls can still cancel or complete the dialog.

![Editor confirmation after context restoration](screenshots/webgl2-restored-editor-dialog.png)

## Editor save durability

Map-editor writes now use checked atomic replacement and retain the save dialog
until durable persistence succeeds, including Save before quit. Quota and
transaction failures provide retry and export without losing the open editor.
Six new failure/export/retry scenarios and six existing editor save/cancellation
scenarios pass across Chromium, Firefox and WebKit. Native client/Wasm builds,
the native save-safety harness and the expanded native session harness pass.
The native regression checks save/reload and preservation of live metadata when
replacement fails. The existing fixture is restored before subsequent
scheduled-versus-synchronous comparisons.

See [storage behavior and cancellation semantics](storage.md#editor-saves).

![Editor save failure with export and retry](screenshots/editor-save-failure.png)

## GPU generation fixture correction

The Firefox/WebKit WebGL core run completed with 45 of 46 checks passing. The
WebKit concrete-islands case timed out waiting for the editor because the game
had shown its explicit generation-failure dialog; it was not a stalled GPU.
The editor takes its generation seed from wall time, so the test previously
requested a different map on every run. The browser generation tests now inject
a fixed wall clock corresponding to seed 12345, matching the native generation
fixture, while leaving animation and cooperative timers running normally.
No production behavior or test deadline was changed.

All six fixed-seed generation/cancellation cases now pass with WebGL across
Chromium, Firefox and WebKit. The six new editor persistence-failure cases also
pass with WebGL across those engines. An intermediate test installed the fixed
clock after startup and reported two WebKit SDL audio-buffer errors; the final
fixture installs it before runtime initialization and passes. This does not
establish that all audio activation races are resolved.

## Protocol 29 admission milestone

Browser and native clients now require an exact protocol match in both greeting
directions before credentials are sent. YOG rejects authentication out of order,
repeated greetings and attempts to replace an authenticated identity. Update
browser assets, desktop clients and YOG together: this is a protocol change.
See [the wire contract and remaining authorization work](protocol.md).

The full multiplayer matrix passed 30 scenarios across Chromium, Firefox and
WebKit, including browser/browser and browser/native matching-checksum matches.
A subsequent translation correction passed all nine mismatch UI scenarios with
visible-text assertions and the native session harness. Native client, headless
server and Wasm builds and the transport harness passed. Capability/data hashes,
room-specific authorization and recovery remain open; this is not public-service
security qualification.

![Actionable protocol mismatch](screenshots/incompatible-release.png)

## Review screenshots

The following is an uncomposited full Firefox window on macOS, with the address
bar and tab visible, running the local WebGL2 build. Gameplay and editor captures
above show the game surface.

![Glob2 in a full Firefox window](screenshots/firefox-window-menu.png)

## Full WebGL single-player baseline

At browser-defaults commit `6d1d8bbae`, all 114 selected single-player scenarios
passed with `GLOB2_TEST_RENDERER=webgl2` across Chromium, Firefox and WebKit on
macOS arm64 (19.3 minutes). This covers campaigns, editor, import/export,
persistence failures, gameplay, rendering/context restoration and viewport
behavior. The explicit software-fallback case remains software by design.
Command: `GLOB2_TEST_URL=http://127.0.0.1:18770 GLOB2_TEST_RENDERER=webgl2 npx playwright test single-player viewport rendering storage import campaign-progress editor-storage file-selection`.

This closes the previously incomplete full cross-engine single-player GPU test
pass on this host. It does not establish controlled performance baselines,
actual Safari/Edge coverage, the supported previous-major matrix, multiplayer
recovery, or freedom from all audio races. Software remains the default.

## Settings persistence and broader GPU gates

Preferences and both keyboard-layout writers now return checked atomic-write
results. Settings remains open until host persistence completes; failures offer
Retry and Continue with no promise that Continue saved or discarded local files.
The same screen state machine runs on desktop and in the browser. See the
[preferences contract](storage.md#preferences-and-keyboard-bindings).

At `46a4d56d0`, the new settings cases plus existing rendering/context-restoration cases pass
all 24 scenarios under WebGL across Chromium, Firefox and WebKit. The new native
checks verify preference/keyboard round trips, preservation of prior bytes on
write failure, and that Settings closes only after persistence completion.
The save-safety/session harnesses, desktop/Wasm builds, 11 browser unit tests and
nine build-system tests pass. A real headed Chromium WebGL test also verifies
background-tab suspension and return without advancing overdue ticks.

CI now exercises WebGL gameplay, settings storage, rendering and viewport cases
in addition to the default suite, and runs real background-tab checks with both
renderer selections. These CI additions are locally checked coverage changes,
not a claim that the hosted workflow has already completed.

![Settings failure retains Retry and Continue](screenshots/settings-save-failure.png)

The four settings cases also pass with Chromium's software renderer. Real headed
visibility checks pass with both renderer selections; software passed once and
then three consecutive repeats, and the final WebGL fixture passed once.

An initial software visibility run completed its background checks but missed
the Quit interaction. A diagnostic attempt incorrectly equated menu visibility
with the explicit pause flag, and another run missed map selection. The final
fixture waits for the presented Quit label, preserves all timing assertions,
and retains a screenshot, state and delivered-input diagnostics on failure.
The subsequent passes do not prove every headed input/focus race is resolved;
that qualification remains open. No production input behavior or deadlines were
changed to obtain these results.
