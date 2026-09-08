# Implementation status

Browser support remains experimental. This ledger distinguishes delivered
infrastructure from the supported-release acceptance criteria.

## Delivered in this branch

- Shared SCons source manifests and native/web configuration isolation,
  including generated headers, compilation databases, objects, caches,
  signature databases, temporary directories, and build-directory locking.
- SDK revision selection and checksummed Emscripten ports, including Boost.
- Native desktop, headless lobby, headless router, WebSocket gateway, and
  browser build entry points. The headless router defaults to a local lobby;
  `GLOB2_YOG_HOST` selects another private lobby.
- A bounded fixed-backend gateway with origin checks, backpressure,
  health/metrics endpoints, and real TCP/WebSocket integration tests.
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
  cancellation preserves unsaved edits. Old-format loading still uses the
  synchronous adapter, and file serialization/durable storage remain transitional.
- Explicit cooperative game/map loading jobs and a cancellable editor-entry
  loading screen with staged ownership and RNG restoration. Terrain chunks and
  gradient boundaries yield; individual expensive operations and other loading
  callers still need migration and latency validation. Single-player startup
  (custom/save/replay/campaign) now owns the engine in a cancellable loading
  screen, including replay cleanup and error transitions. In-editor replacement
  retains the current map until a new one loads successfully; cancellation and
  failure preserve its unsaved edits.
- A maintained, dependency-locked Playwright suite with real input and
  read-only diagnostics, plus CI failure traces.
- A replay-stall fix: measure the waiting-player mask after local orders are
  inserted, so filtered replay null orders cannot leave a stale waiting flag.

## Local validation

On macOS arm64, September 2026:

- Native desktop, lobby, router, gateway, and release Wasm builds succeed.
- Nine build identity/architecture tests and eight gateway integration tests pass.
- The native speed regression passes live speed, pause/hard pause, replay
  playback, and all seven expected checksum samples. The camera test now
  samples both cadences at the measurement window's end; previously the slower
  cadence sampled before its final sleep. Tolerances remain unchanged.
- The screen lifecycle/stack harness passes. The incremental engine harness
  produces matching 50-tick checksums with regular callbacks, delayed callbacks,
  and a stack-driven game session,
  checks lifecycle guards, and verifies repeatable delay queries. The editor
  harness checks incremental quit, cancellation, and discard, plus fertility
  equality against the previous algorithm at three work budgets, progress,
  deferred publication, and cancellation through the real progress screen.
  Cooperative loading tests cover nested lifetime/exception behavior, partial
  allocation cleanup, RNG restoration, and matching loaded-game checksums.
  Scheduled game initialization also matches the 50-tick session checksum;
  cancelled game/replay starts and missing replay failures release global state.
  Failed and cancelled editor replacements preserve map checksums, RNG, and the
  unsaved-edit prompt. Child transitions clear held input without changing focus.
  The coroutine lifecycle tests also pass with AddressSanitizer.
- Thirty-six browser checks cover startup, settings/credits/shutdown,
  editor/campaign-entry navigation and map quit decisions, campaign selector
  cancellation/reopen,
  custom options/AI descriptions and return-to-setup,
  tutorial launch, custom-game pause, save/reload byte
  equality, editor save cancellation and map reload persistence, load continuation,
  editor load cancellation/restart and staged replacement, game-start cancellation/retry, and
  audio-context activation are exercised
  in Chromium, Firefox, and WebKit using Playwright 1.63.0.
- Alternating native/browser builds preserve compilation output contents
  and timestamps and do not change tracked files. CI also defines separate
  clean native-first, web-first, and concurrent jobs; CI results are not
  certified by a local run.

These are focused regressions, not a complete campaign, AI, deterministic
cross-platform, or supported-browser certification matrix.

## Still required for the supported release

1. Complete dependency archive verification and reproducible release gates
   on all supported native toolchains.
2. Complete editor/network/modal transitions, resumable/cancellable loading,
   and removal of Asyncify from the scheduled browser host.
3. WebGL2 rendering, live logical resize, context restoration, minimum-size
   UI, focus/visibility behavior, and complete browser interaction handling.
4. Transactional persistence with durable completion and failure states,
   quota handling, and validated import/export for all local data types.
5. Injectable TCP/WebSocket/WSS transports, compatible protocol handshake,
   bounded protocol parsing, and deterministic browser/native cross-play.
6. YOG guests/accounts, invitations, room controls, password migration, and
   coordinated 120-second checkpoint-based recovery with fault injection.
7. Versioned self-hosting distribution, TLS deployment tests, backup/migration,
   draining/rollback, operations documentation, and performance/soak gates.

No cross-play, reconnect, WebGL2, durable-save failure recovery, or stable
browser support is claimed by this implementation slice.
