# ADR 001: one source manifest, independent build identities

Status: implemented; platform-wide release validation remains in progress.

The experiment parsed SConscript text and used host Boost headers. Native
configuration wrote a root header and options cache. Running different
toolchains in the same checkout could therefore change another build's inputs.

SCons now imports plain Python source manifests from `scons/sources.py`.
Native and Emscripten code paths branch before dependency discovery. Every
toolchain/role/configuration owns a directory, signature database, generated
header, compilation database, compiler outputs, and temporary directory.
Browser ports and compiled SDK libraries share a cache keyed by the pinned SDK
and port configuration, with Emscripten locking and process-held resource leases.
`GLOB2_DEV_MODE=isolated` retains identity-local caches for clean verification.

Source includes name `glob2/BuildConfig.h`, resolved through the target's
generated include directory. An unrelated root `config.h` cannot satisfy this
include. Generated headers are atomically replaced only when contents change.
An identity marker rejects reuse of an output directory by an incompatible
configuration. An OS file lock rejects simultaneous writers of the same
directory; different identities can build concurrently.

Options are explicit on each command. The emitted options record is not loaded
by another invocation. Existing native selectors remain available, including
`role=relay`, `mingw=1`, `mingwcross=1`, and `--build=PATH`. Their default output
paths move under the identity directory. macOS packaging is an explicit
`package` target rather than a side effect of compiling release objects.

The browser SDK revision and version are recorded in
`browser/toolchain.json`. Emscripten verifies port archive checksums. A complete
release dependency lock covering SDK archive digests and native gateway
dependencies is still required before reproducible release status.

CI validates the identity rules directly and builds native and WebAssembly
outputs in their own jobs. `test/build_system/coexistence.py` remains an
explicit diagnostic for concurrent and alternating builds; it verifies that
object and artifact contents, timestamps, and tracked source files remain
unchanged. Running that full native build again in the browser job would
duplicate the Linux lanes without improving routine pull-request coverage.
These checks do not replace platform-specific runtime and determinism tests.

SDL3, SDL3_image, SDL3_ttf and SDL3_net are built into the selected browser
output directory using the shared checksum-verified dependency helper. SDL2
Emscripten ports are not used. The FreeType source dependency is separately
pinned in `scons/sdl3-vendored.json`; other existing Emscripten ports retain their
SDK pins. Browser transport uses WebSockets and does not initialize SDL_net's
native resolver threads or LAN discovery.


## Development build variants

`python3 tools/dev_build.py target=web` builds only the threaded development
runtime. Use `web_variant=serial` where cross-origin isolation, shared memory or
worker rendering is unavailable. A threaded-only package reports unsupported
hosts instead of attempting to fetch a missing serial runtime. Music, recording,
Hive, scripting, assets and localization remain available in either variant.
Ordinary `scons target=web` defaults to both variants; `web-package` requires both.
Browser SDL/Opus installations are shared by SDK and threading configuration,
while the standalone music decoder always uses serial Opus libraries.
See [fast development builds](../development/reference.md#fast-development-builds)
for PCH/unity options, caching, dependency jobs and measurement limitations.
