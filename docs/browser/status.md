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
  now drives the campaign new/load selector with deferred transitions. Other
  menus, mission execution, and the top-level host still require migration.
- A maintained, dependency-locked Playwright suite with real input and
  read-only diagnostics, plus CI failure traces.
- A replay-stall fix: measure the waiting-player mask after local orders are
  inserted, so filtered replay null orders cannot leave a stale waiting flag.

## Local validation

On macOS arm64, September 2026:

- Native desktop, lobby, router, gateway, and release Wasm builds succeed.
- Nine build identity/architecture tests and eight gateway integration tests pass.
- The native speed regression previously passed live speed, pause/hard pause,
  replay playback, and all seven expected checksum samples. After screen-phase
  extraction, two runs stopped at the camera-cadence assertion (10/13 cells,
  tolerance 2) before reaching replay checks. Background CPU load was high;
  the cause is not established and this rerun remains an open validation item.
  The new screen lifecycle harness passes, including creation/input/timer
  completion, reuse, supplied event modifiers, and the compatibility host.
- Twelve browser checks cover startup, campaign selector cancellation/reopen,
  tutorial launch, custom-game pause, save/reload byte
  equality, load continuation, and audio-context activation are exercised
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
2. Shared application update API, screen stack, explicit modal completion,
   resumable/cancellable loading, and removal of Asyncify.
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
