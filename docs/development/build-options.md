# Development build options

Fast builds accelerate editing; ordinary debug builds provide full symbols and release builds provide representative runtime behavior.

Native distribution packages that extract separate debug symbols can use
`release=1 retain_symbols=1`. This preserves release optimization and the free
launch profile while keeping compiler debug information and disabling stripping
by SCons. The RPM recipe uses this mode so Fedora can produce its debug packages.
Its output directory is separate from ordinary stripped release builds. Other
release packages keep their existing stripping behavior; macOS already retains
symbols for its matching dSYM.

Windows builds use MinGW's existing pthread runtime for Boost.Asio in every
translation unit (`BOOST_ASIO_HAS_PTHREADS=1`). Keep this build-wide: header-order
detection can otherwise mix POSIX and Win32 Asio thread layouts, corrupting
network teardown. The pthread runtime is already required by the Windows build.

## Fast development builds

`python3 tools/dev_build.py` enables caching and `dev_fast=1` in a separate output
directory, forwarding SCons targets and options. It defaults to at most eight
jobs and `linker=auto`. The wrapper preserves the selected client profile and normal release compiler flags.
Install ccache before using the development command. Native dependencies still
come from the existing package-manager or `GLOB2_SDL3_PREFIX` configuration;
mobile dependencies and SDKs still require their existing setup steps.

```sh
python3 tools/dev_build.py
python3 tools/dev_build.py engine-tests -j8
python3 tools/dev_build.py dev_fast=0 linker=default
python3 tools/dev_build.py pch=1 unity=1
python3 tools/dev_build.py target=web web_variant=serial
python3 tools/dev_build.py target=web web_variant=serial web_legacy_gl=0
python3 tools/dev_build.py target=android arch=arm64-v8a
python3 tools/dev_build.py target=ios environment=simulator
python3 tools/dev_build.py --cache-stats
```

Fast builds use `-O0 -g1`: backtraces and basic symbols remain, but full variable
inspection requires ordinary debug compilation. Fast/PCH/unity options reject
release or profiling builds. Use release builds for shipping and performance
measurements. Preserve numeric guards and compare simulation checksums when
validating build configurations; do not bump the simulation revision to mask a
build-induced mismatch.

`pch=1` precompiles stable standard headers separately for each compiler/flag
configuration. Its C++ objects bypass ccache rather than weakening cache
correctness; C and Objective-C++ objects bypass PCH. `unity=1` combines only the
checked-in allowlist in deterministic groups of at most four files, preserves
ordinary source commands for IDEs, and reuses production objects in harnesses.
Both experiments default off; their speed benefit must be measured.

`linker=auto` uses installed LLD for native Linux ELF builds and otherwise keeps
the platform linker. `linker=lld` fails on unsupported targets or missing tools.
Android retains its NDK linker, and Apple/browser builds retain their existing
linkers. `dependency_jobs=N` overrides dependency parallelism, which otherwise
follows SCons `-j`.

Recording and browser SDL/Opus installations are shared by recipe, pinned inputs,
compiler, SDK, architecture and ABI configuration. Publication is staged and
content verified under leases; active installations cannot be pruned. Upstream
builds use temporary paths without spaces, then their verified installations are
copied to the cache volume before atomic publication. This permits the macOS
`Application Support` cache location without passing it to upstream Makefiles.
Pinned SDL and recording archive downloads retry network failures and transient
HTTP responses up to three attempts, with one- and two-second delays. Every
archive must match its locked SHA-256 before installation; checksum failures
and permanent HTTP errors fail immediately.
Explicit
prefix overrides and `GLOB2_DEV_MODE=isolated` remain supported. Clean, dry-run,
help and compilation-database-only requests do not provision dependencies.
A cold native dry-run can still require an existing SCons configure directory.
Debug prefix mapping in fast builds uses `.`; run the debugger from the checkout
or set its source substitution directory when debugging elsewhere. Runtime paths
and `__FILE__` semantics are preserved.

`CCACHE_DEBUG=1 CCACHE_DEBUGDIR=/absolute/path` enables miss diagnostics through
the wrapper. Do not add sloppiness settings. Inspect cache statistics and eviction
before increasing budgets or attributing poor reuse to source size.

Run `python3 tools/benchmark_build.py --help` for isolated comparisons. The default
is three repetitions of dependency setup, uncached compilation, warm-cache
rebuilding, no-change builds, source edits and header edits. Reports include exact
commands, compile databases, dependency manifests, cache deltas and object sizes.
Compile/link work totals sum command durations; they are not parallel wall times.
GNU time reports per-process peak RSS on Linux; unsupported hosts report missing
memory coverage explicitly. Review evidence stays under `artifacts/dev-build`.
No speedup is assumed before measuring matching source and toolchain inputs.

### Shared renderer shader assets

The build generates shader layouts and GLSL variants from
`libs/gag/shaders/renderer-manifest.json` and the authoritative material sources.
`GLOB2_GLSLANG` can point to pinned glslang 15.1.0 for offline Vulkan SPIR-V generation;
without the platform’s offline binaries the experimental SDL GPU family is unavailable.
`GLOB2_NAGA` selects pinned Naga 27.0.0 for browser WGSL. Optional
`GLOB2_SPIRV_CROSS` and `GLOB2_DXC` select the pinned build-only native translators
described in the rendering guide; together with glslang they produce Direct3D 12
DXIL. On Apple build hosts, `GLOB2_METAL_SDK` and
`GLOB2_METAL_TOOL_LOCK` select the verified offline Metal SDK and tool receipt;
`DEVELOPER_DIR` scopes the selected Xcode installation. `GLOB2_RENDER_SHADER_INPUT`
imports prebuilt shaders only after validating source, layout and artifact
receipts. These paths are described in the rendering guide. No shader translation compiler ships with the game. Generated native assets install under
`glob2/shaders`; browser GLSL assets are preloaded under `/shaders/webgl2`.
The browser shader preloads are SDK payloads beside each runtime: `index.data`
and `threaded/index.data`. Content-addressed artwork packages under `assets/`
remain shared. Local deployment through `deploy/install-web-client.py` checks
both runtime payloads and the presentation-surface helper before replacing served
files, then publishes these dependencies before the game scripts.
`GLOB2_RENDERER_SHADER_DIR` overrides the shader directory for focused device tests.

## Free launch profiles

Client releases default to `client_profile=free`; development builds default to
`full`. Free builds omit Commander UI, startup, credit links and paid authoring
links while retaining ordinary play and community libraries. Use
`client_profile=full` explicitly to develop unfinished features. These switches
control access paths, not simulation rules, save formats or the interpreter
needed to read existing saves and replays.

`distribution` accepts `direct`, `browser`, `steam`, `epic`, `google_play`,
`app_store`, `microsoft`, `amazon` and `china`. Defaults are `browser` for web,
`google_play` for Android, `app_store` for iOS, and `direct` for native builds.
Free App Store builds also hide community JavaScript AI and generator discovery,
installation and room authoring. Other free distributions retain them. Review
store requirements separately; a feature profile does not establish acceptance.

Each capability can be overridden with `feature_commander`,
`feature_authoring_links`, `feature_community_ai` and
`feature_community_generators` (0 or 1). Restricted China/Amazon editions still
force Commander and authoring links off. Resolved capabilities are recorded in
`identity.json` beside the build outputs and in generated `BuildConfig.h`.
Distinct profiles use isolated output directories; dependencies remain shared.
An explicit `--build` directory refuses an incompatible existing identity.

```sh
python3 tools/dev_build.py client_profile=free
scons release=1 client_profile=free
scons release=1 client_profile=free distribution=app_store bundle
scons target=ios release=1 client_profile=free
```

Use standard release packaging for shipping. Fast/PCH/unity development builds
are unsuitable for release qualification. Keep the actual producer
`identity.json` with final-package evidence; do not reconstruct it from an
intended command after signing.

When changing Commander or authoring behavior, verify both free and full client
profiles. A free-profile test cannot establish the behavior of a disabled paid
feature. The native `ClientCapabilities` suite checks exposed categories, and
`HiveMindIntegration` checks that a free client makes no Commander requests and
preserves existing checkpoints.

## Gradient compute assets and Apple runtime

All native clients compile the optional Vulkan compute adapter; a missing or
unsupported driver uses CPU fallback. Vulkan-Headers **1.2.162** are vendored under
`third_party/vulkan` with their licence and source hashes; they are build inputs,
not a linked SDK. No Vulkan loader is bundled on Linux, Windows or Android.

`src/field/shaders` owns the two compute programs. Normal builds verify the
embedded bundle and need no compiler. Regenerate after source/tool changes with:

```sh
python3 tools/shaders/gradient_compute.py --glslang /path/to/glslang --naga /path/to/naga
```

Use glslang **15.1.0** and Naga **27.0.0**. The generated header embeds SPIR-V,
WGSL and a source identity; the manifest hashes sources and generated artifacts.
Verification requires the complete receipt set, including the generator and
pinned Vulkan headers; missing receipts or a changed target fail the build.
Shader translation tools remain development dependencies.

Apple packaging fetches hash-pinned MoltenVK **1.4.2** using `scons/moltenvk.json`.
macOS bundles select the executable's architecture and include its framework and
Apache-2.0 notice; iOS device/simulator projects embed and sign the matching slice.
Downloads and extraction publish to the dependency cache only after validating
the archive, selected framework binary, slice metadata and licence. Cached runtime
receipts are rechecked before reuse; interrupted extraction cannot publish a
partial package. Remove a damaged dependency cache to retry from the pinned archive.
The unthinned macOS binary is about 10.3 MiB; iOS device is about 4.6 MiB, excluding
framework metadata. These are upstream input sizes, not measured compressed
package growth. Adding MoltenVK is a runtime dependency change requiring explicit
maintainer approval and Apple packaging/device qualification. Runtime selection
remains independent of rendering and never ships a shader compiler.

## Recording codec builds

Native Linux clients can select `recording_codec=h264` (the default),
`recording_codec=vp9`, or `recording_codec=av1`:

```sh
scons release=1 recording_codec=vp9
scons release=1 recording_codec=av1
python3 tools/dev_build.py recording_codec=vp9 unit-tests
```

The WebM profiles replace H.264/AAC with VP9/Opus or AV1/Opus throughout the
recorder. They neither fetch nor link x264 or NVIDIA codec headers, and do not
compile H.264 or AAC encoders. They use pinned static libvpx 1.17.0 (VP9) or
libaom 3.15.1 (AV1), together with the native client's existing Opus dependency
through `pkg-config`. There is one native Opus implementation; the recorder does
not bundle a second copy. libvpx uses a BSD license; libaom includes BSD and AOM
patent license terms. The dependency recipe
installs license and patent notices with the selected sources and configuration.
AV1 builds also require CMake. Only the selected video library is built.

Each profile has a separate output directory and dependency-cache identity.
The native Opus version, compiler flags and link flags are recorded in the
recording receipt and included in its cache inputs.
Explicit `GLOB2_RECORDING_PREFIX` overrides must use a separate prefix for each
codec. A prebuilt SDL recording prefix is reused only for the default H.264
profile. WebM profiles currently support native Linux clients; other targets
reject them before provisioning dependencies. Both use software encoding, so
qualify gameplay frame times and dropped-frame counters on the target hardware;
realtime settings alone do not establish realtime performance.

See [gameplay recording](../features/gameplay-recording.md) for output paths,
quality controls and recovery.
