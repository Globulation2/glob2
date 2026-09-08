# Browser platform implementation

The browser target provides desktop-browser single-player and existing YOG
multiplayer: login, lobbies, room setup, joining, browser/browser matches, and
matching-release browser/native matches. It shares game logic, deterministic
simulation, save/map formats, and the YOG wire protocol with native builds.

Guest identities, private invitations, cloud saves, late joining, backend restart
recovery, mobile UI, voice chat, and rankings are outside this change. Refreshing
or disconnecting during a match ends that player's participation.

LAN is compiled out of the WebAssembly client because browser sandboxing cannot
provide Glob2's direct TCP listener and discovery model. The longer-term browser
multiplayer direction is a web entry flow over YOG: shareable match links,
lightweight or guest identity, and instant matchmaking. This PR provides the
cross-play transport and existing lobby flow; it does not implement that product
experience or publish a Play button on the project website.

## Architecture

- SCons is the source of truth for every toolchain. Plain Python source manifests
  are shared by native and web targets; target identities keep generated output
  isolated.
- `Application` and `ScreenStack` own interactive navigation. The browser host
  schedules frames; browser-reachable loading and generation use cooperative
  jobs rather than suspended C++ stacks.
- Game and AI code do not call browser APIs. The browser platform owns frame
  scheduling, viewport and visibility events, file selection, storage, audio
  activation, transport, and read-only diagnostics.
- Browser storage acknowledges a write only after `FS.syncfs` succeeds. Import,
  export, retry, rollback, restore failure, and shutdown use the same durable
  storage path.
- WebGL2 and software rendering share the renderer interfaces. Context recovery
  rebuilds renderer resources while retaining the current application state.
- YOG continues to own identities, rooms, and match lifecycle. The WebSocket
  gateway relays framed bytes to a fixed native backend and does not participate
  in simulation.

## Build identity

Default outputs are `build/<toolchain>/<role>/<mode>`. `--build=PATH` overrides
that path only when it belongs to the same identity. `identity.json` records
ownership, and generated configuration is written to
`include/glob2/BuildConfig.h`.

Existing native commands retain their roles. For example, `scons release=1`
builds the desktop client, while `scons target=web release=1` writes the browser
application to `build/emscripten/client/release`. The compatibility command
`python3 browser/build.py` delegates to SCons.

Browser and native multiplayer clients must use the same protocol version.
Update the client, YOG services, and gateway deployment together. See the
[protocol contract](protocol.md) and [gateway guide](gateway.md).

## Verification

CI builds native client/server, router, gateway, and browser identities. Native
harnesses cover screen/session ownership, loading and generation cancellation,
save safety, transports, and deterministic replay. Chromium runs the complete
browser behavior suite; Firefox and WebKit run focused startup, gameplay, and
viewport compatibility checks. Focused Chromium runs cover WebGL2 and real-window
visibility in addition to the software-renderer suite. Persistence, import/export,
context recovery, YOG, and browser/native cross-play remain in the complete suite.
Manual workflow runs accept `browser_only` when a follow-up changes only the web
host or its tests; ordinary pull requests and pushes still run every platform job.

The operational commands live in [the browser README](../../browser/README.md).
WebKit automation is not a substitute for manual testing in shipping Safari, and
Chromium automation is not a substitute for shipping Edge qualification.
