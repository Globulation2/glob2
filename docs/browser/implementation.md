# Browser platform delivery

The browser target is under development, not a supported release. The release
requires desktop browser single-player support, matching-release native
cross-play, YOG invitation rooms with guests and accounts, 120-second coordinated
reconnect, and self-hosted distribution. Mobile, voice chat, rankings, cloud saves,
late joining, and backend-restart match recovery are excluded.

## Architecture contracts

- SCons is the source of truth for every toolchain. Source manifests are plain
  Python tuples; target/configuration identities own all generated outputs.
- Game and AI code must not call browser APIs. Platform implementations own
  scheduling, graphics, storage, audio activation, and transport.
- YOG owns identities, rooms and match lifecycle. A fixed-backend WebSocket
  gateway owns transport only. Simulation remains deterministic client lockstep.
- Durable persistence acknowledgment must follow successful storage completion.
- Resize is an application event applied between frames, never a reload.
- A reconnect checkpoint must include simulation and network continuation state,
  exclude another player's local UI state, and pass checksum verification.

## Release gates

- [ ] Build coexistence across native client, lobby, router, gateway, and web
- [ ] Explicit application/screen scheduling without Asyncify
- [ ] WebGL2 rendering with context restoration and software fallback
- [ ] Live resize and focus/visibility lifecycle
- [ ] Transactional browser storage with import/export and failure handling
- [ ] Browser and native secure transports; compatible protocol handshake
- [ ] Invitation rooms, guests, optional accounts and credential migration
- [ ] Pause barriers, checkpoints and refresh recovery
- [ ] Self-hosting, immutable releases, backups, health checks and metrics
- [ ] Browser, native, deployment, determinism and fault-injection test gates

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
