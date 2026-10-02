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
  schedules frames; loading uses cooperative jobs. Generation uses owned
  preparation screens and worker-backed landscape previews; the serial fallback
  yields between preview candidates (see ADR 005).
- Game and AI code do not call browser APIs. The browser platform owns frame
  scheduling, viewport and visibility events, file selection, storage, audio
  activation, transport, and read-only diagnostics.
- The release packages serial and threaded WebAssembly runtimes. A capability
  probe selects the threaded runtime only after isolation and worker checks pass.
  Engine pools use shared `ThreadSupport` and standard C++ synchronization.
  A browser-only launcher owns the application pthread; UI/DOM operations are
  proxied to the main runtime. WebGL transfers its canvas to the application
  worker; SDL software presentation stays on the UI thread. Scheduled application
  frames publish diagnostics at their boundaries. Worker allocation failures use
  existing serial pool fallbacks; runtime failures do not restart a running game.
- Browser storage acknowledges a write only after `FS.syncfs` succeeds. Import,
  export, retry, rollback, restore failure, and shutdown use the same durable
  storage path.
- WebGL2 and software rendering share the renderer interfaces. Context recovery
  rebuilds renderer resources while retaining the current application state.
- YOG continues to own identities, rooms, and match lifecycle. The WebSocket
  native WSS transport delivers framed bytes directly and does not participate
  in simulation.

The pinned SDL port creates its WebGL context through EGL. The threaded build
uses `browser/threaded-egl.js` to keep the SDK's EGL calls and state on the
application pthread instead of proxying them to the UI, where the transferred
canvas cannot create a context. This adapter is included only in the threaded
runtime and must be checked when upgrading the SDK or SDL port.

`browser/Audio.cpp` bridges the pinned SDL WebAudio driver: device creation and
output stay on the UI thread, while mixer callbacks run on the application
thread with engine mutations. The UI consumes the previous PCM block without
waiting; missed deadlines produce silence and the queue stays bounded. This
adds one device buffer of audio latency and keeps SDL calls out of engine
platform branches. Closing the UI device establishes a callback barrier, then
drains disabled mixer requests on their owner before the application pthread exits.

The WebSocket adapter copies transient SDK events on the UI thread and applies
them on the owning application thread. Its bounded callback queue preserves
message order and discards callbacks after the transport closes. Each connection
owns its proxy queue; synchronous deletion on the UI precedes draining disabled
events and releasing that queue. This reclaims callback arguments that the SDK
would otherwise discard without destructors during pthread shutdown.

The storage adapter captures an immutable IDBFS snapshot before the asynchronous
IndexedDB scan. Worker filesystem operations can continue while it commits;
atomic renames cannot invalidate the captured paths or change their bytes.
The existing storage generations still gate save completion and retries.

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
Update the client and YOG services together. See the
[protocol contract](protocol.md) and [secure transport guide](gateway.md).

## Verification

CI builds native client/server, router, and browser identities. Native
harnesses cover screen/session ownership, loading and generation cancellation,
save safety, transports, and deterministic replay. Chromium runs the complete
browser behavior suite; Firefox and WebKit run focused startup, gameplay, and
viewport compatibility checks. Focused Chromium runs cover WebGL2 and real-window
visibility in addition to the software-renderer suite. Persistence, import/export,
context recovery, YOG, and browser/native cross-play remain in the complete suite.
Manual workflow runs accept `browser_only` when a follow-up changes only the web
host or its tests. Pull requests select native, browser, map-generator, and
self-hosting deployment jobs from changed paths. AI, GUI, rendering, and networking
changes skip the map-generator sweep; browser shell, AI, GUI, and rendering changes
skip the deployment check.
Shared build, data, and cross-platform fixture changes still run every platform
job and the checksum comparison. An unreadable diff also runs every job. Pushes
to `master` run the full workflow and refresh compiler caches. A final check
verifies that every selected job succeeded.

The operational commands live in [the browser README](../../browser/README.md).
WebKit automation is not a substitute for manual testing in shipping Safari, and
Chromium automation is not a substitute for shipping Edge qualification.

CI retains the same 1,500-tick `games/cross-replay.game.gz` trace from two Linux
compiler environments, Windows, and serial/threaded WebAssembly, then compares every byte in a
separate comparison job. The fixture uses seed 42. Run a native trace locally with
`python3 test/run-browser-determinism.py BINARY OUTPUT`; the browser counterpart
is `browser/tests/determinism.spec.js`. Artifacts include logs, fixture hashes,
and trace hashes. This scenario complements the save-continuation harnesses;
it does not establish equivalence for every game or generator.

## Compressed release assets

The release build preloads the shared verified runtime export, including WebP
support. The pinned Emscripten SDK's standard SDL_image port lacks WebP, so
`browser/ports/` supplies checksum-pinned libwebp 1.6.0 and SDL_image 2.8.12 ports
with PNG/JPEG/WebP loading. Their recipes participate in the managed port cache
identity; source/debug images remain PNG. Codec notices ship with runtime data.
`browser/package-static.py` creates deterministic gzip sidecars for the
versioned serial and threaded JS/WASM, the capability loader, shared data file,
and HTML entry point. Both runtime binaries and the loader participate in the
package identity; the loader selects versioned URLs from the entry point.
Packaging verifies both runtime asset payloads are identical before retaining
one shared data file. Its packaging policy
participates in the version identity so changing transfer representation does
not overwrite an older immutable URL. `--verify DIRECTORY` checks file coverage,
SHA-256 and each sidecar's decompressed bytes before publication.
The generated `package.json` marker records ownership and package identity.
Static export directories use mode 0755 and files 0644 so an unprivileged
server can read the payload even when the build uses a private umask.
Packaging refuses to replace an unmarked directory, even if it contains HTML;
remove an older generated `build/browser-static` once before repackaging it.

Static exports use `0755` directories and `0644` files so the web container can
read the public package as its separate user, including threaded assets and gzip
sidecars, regardless of the packager's umask.

Caddy serves gzip sidecars through content negotiation and retains original files
for uncompressed requests. The Google Cloud Storage publisher uploads gzip bytes
at the logical asset URLs with their original MIME types and
`Content-Encoding: gzip`, with immutable asset caching and the entry point last.
It omits `no-transform` so Storage can decompress for clients without gzip.
The direct Storage URL provides the serial fallback because it lacks browser
isolation headers. For a threaded public release, serve the package through the
provided Caddy configuration or an equivalent HTTPS frontend and set the repository
variable `GLOB2_BROWSER_PUBLIC_URL` to its game entry point. The release workflow
checks HTTPS (including redirects), COOP `same-origin` and COEP `require-corp`
before advertising that URL. Without that variable, publication explicitly labels
the direct Storage URL as serial. The frontend must also serve the versioned loader,
shared assets and both runtime directories from the same origin.
JavaScript, streaming WASM and Emscripten data loading consume the original
uncompressed representation through the browser's HTTP decoder. Verify transfer
headers, progress and actual compressed bytes on the release host; local static
files alone do not establish deployed behavior.
