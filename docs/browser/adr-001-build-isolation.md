# ADR 001: one source manifest, independent build identities

Status: implemented; platform-wide release validation remains in progress.

The experiment parsed SConscript text and used host Boost headers. Native
configuration wrote a root header and options cache. Running different
toolchains in the same checkout could therefore change another build's inputs.

SCons now imports plain Python source manifests from `scons/sources.py`.
Native and Emscripten code paths branch before dependency discovery. Every
toolchain/role/configuration owns a directory, signature database, generated
header, compilation database, compiler outputs, and temporary directory.
Browser ports and compiled SDK libraries use that identity's cache.

Source includes name `glob2/BuildConfig.h`, resolved through the target's
generated include directory. An unrelated root `config.h` cannot satisfy this
include. Generated headers are atomically replaced only when contents change.
An identity marker rejects reuse of an output directory by an incompatible
configuration. An OS file lock rejects simultaneous writers of the same
directory; different identities can build concurrently.

Options are explicit on each command. The emitted options record is not loaded
by another invocation. Existing native selectors remain available, including
`server=1`, `mingw=1`, `mingwcross=1`, and `--build=PATH`. Their default output
paths move under the identity directory. macOS packaging is an explicit
`package` target rather than a side effect of compiling release objects.

The browser SDK revision/version and Boost port version are recorded in
`browser/toolchain.json`. Emscripten verifies port archive checksums. A complete
release dependency lock covering SDK archive digests and native gateway
dependencies is still required before reproducible release status.

CI validates the identity rules directly and builds native and WebAssembly
outputs in their own jobs. `tests/build_system/coexistence.py` remains an
explicit diagnostic for concurrent and alternating builds; it verifies that
object and artifact contents, timestamps, and tracked source files remain
unchanged. Running that full native build again in the browser job would
duplicate the Linux lanes without improving routine pull-request coverage.
These checks do not replace platform-specific runtime and determinism tests.
