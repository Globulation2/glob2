# Browser platform delivery

The browser target is under development, not a supported release. Following the
user's September 8 scope reduction, the release targets desktop browser
single-player and existing YOG multiplayer: login, lobbies, room setup, joining,
browser/browser and matching-release browser/native matches. Include gateway
setup documentation and tests of complete matches. Compatibility checks and safe
message handling remain required for the multiplayer that ships.

Guest identities, private invitations, account-system modernization, coordinated
120-second recovery/checkpoints, host migration, and production hosting automation
and operational tooling are deferred. Refreshes and disconnections may end a
player's participation; this release must not advertise seamless recovery or
modernized account security. Existing development deployment files remain useful
but are not proof of production qualification. Single-player, architecture,
build coexistence, testing, screenshots and review requirements are unchanged.
Mobile, voice chat, rankings, cloud saves, late joining, and backend-restart
match recovery remain excluded.

## Architecture contracts

- SCons is the source of truth for every toolchain. Source manifests are plain
  Python tuples; target/configuration identities own all generated outputs.
- Game and AI code must not call browser APIs. Platform implementations own
  scheduling, graphics, storage, audio activation, and transport.
- YOG owns identities, rooms and match lifecycle. A fixed-backend WebSocket
  gateway owns transport only. Simulation remains deterministic client lockstep.
- Durable persistence acknowledgment must follow successful storage completion.
- Resize is an application event applied between frames, never a reload.
- If implemented later, a reconnect checkpoint must include simulation and network continuation state,
  exclude another player's local UI state, and pass checksum verification.

## Release gates

These checkboxes track full release qualification, not whether code exists.
For implemented features and scoped test results, see [current status](status.md).

- [ ] Build coexistence across native client, lobby, router, gateway, and web
- [ ] Explicit application/screen scheduling without Asyncify
- [ ] WebGL2 rendering with context restoration and software fallback
- [ ] Live resize and focus/visibility lifecycle
- [ ] Transactional browser storage with import/export and failure handling
- [ ] Browser and native secure transports; compatible protocol handshake
- [ ] Existing YOG rooms and complete browser/browser and browser/native matches
- [ ] Documented gateway setup and explicit disconnect/refresh limitations
- [ ] Browser, native, gateway, determinism and applicable fault-injection test gates

Deferred original-plan gates: invitation rooms, guests, account migration,
pause barriers/checkpoint recovery, and production deployment/operations.
Deferral is a scope decision, not a claim these features are implemented.

## Build identity

Default outputs are `build/<toolchain>/<role>/<mode>`. `--build=PATH` overrides
that path, but PATH must belong to the same identity. Mixing identities is an
error. `identity.json` records ownership; generated configuration is
`include/glob2/BuildConfig.h`. Options are explicit on every invocation; emitted
`options.py`/`options.json` records inputs and is not silently loaded.

Existing commands such as `scons release=1`, `scons server=1`, and
`scons mingwcross=1` keep selecting the same kinds of builds. Their default
artifact paths are now isolated. For example, the macOS release client is
`build/darwin/client/release/src/glob2`. Browser output is
`build/emscripten/client/release/index.html`.

The experiment compatibility command `python3 browser/build.py` delegates to
SCons. Emscripten 4.0.15 and its checksum-verified ports (including Boost 1.83)
are selected independently of installed native development libraries.
