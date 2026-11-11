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

## Compiler checks

`python3 tools/compiler/check_stencil_vectorizer.py [--cxx COMPILER]` compiles a
torus 3x3 smoothing stencil shaped like the map generators' kernels at `-O0`,
`-O2` and `-O2` without the vectorizers, under AddressSanitizer, and requires one
checksum from every build and run. It runs on macOS CI and the Linux Clang job,
because an analysis on Apple clang 21 saw `-O2` turn this loop shape into an ASan
heap overflow with run-to-run-varying sums. If it ever fails, check whether the
real stencils in `src/map/generator/shared/` are affected; if they are, add
`-fno-vectorize -fno-slp-vectorize` to the strict numeric sources in
`src/SConscript` for Clang and refresh that platform's generator rows.

## Fast development builds

`python3 tools/dev_build.py` builds the dev tier. On native Linux with CMake
3.25+ and Ninja installed, a request made only of targets, `.`, `-k` and `-jN`
builds with [CMake + Ninja](#cmake--ninja-build-for-daily-work) into
`build/cmake/dev`. Everything else builds with SCons, as before: any SCons
option (`perf=1`, `target=web`, `compile_profile=core`, `pch=1`, `CXX=...`,
`--build=...`, `-n`), a file target, an alias the CMake build does not define,
other host systems, and hosts without CMake or Ninja; the wrapper prints which
build system it chose and why. `--generator=scons` or `--generator=cmake` (or
`GLOB2_DEV_GENERATOR=scons|cmake|auto`) forces the choice.

Through SCons it enables caching and `dev_fast=1` in a separate output
directory, forwarding SCons targets and options. It defaults to one job per CPU
(the [token pool](#parallel-lanes) shares them between concurrent builds) and `linker=auto`. The wrapper preserves the selected client profile and normal release compiler flags.
Install ccache before using the development command. Native dependencies still
come from the existing package-manager or `GLOB2_SDL3_PREFIX` configuration;
mobile dependencies and SDKs still require their existing setup steps.

```sh
python3 tools/dev_build.py                    # glob2, CMake + Ninja on Linux
python3 tools/dev_build.py engine-tests -j8   # CMake + Ninja on Linux
python3 tools/dev_build.py --generator=scons engine-tests
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
release, perf or profiling builds. Preserve numeric guards and compare simulation
checksums when validating build configurations; do not bump the simulation
revision to mask a build-induced mismatch.

## Core compile profile

`compile_profile=core` is an opt-in native development build that leaves out code
most edits never touch, so a change to a core header recompiles fewer files:

```sh
python3 tools/dev_build.py compile_profile=core engine-tests unit-tests
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto-compile_profile-core
```

It leaves out the optional groups `buildsys/sources.py` defines as filters over
`CLIENT_SOURCES`:

- `generators`: the map generators in `src/map/generator/generators/`, except
  the default lobby landscape (`lava-shield`), `uniform`, `hills`, and `forts`
  and `even-ground`, whose blueprints the JavaScript toolkit binds. Generator
  core, shared, compatibility and JavaScript code stay.
- `ai:castor`, `ai:cortex`, `ai:cabino`. Nicowar, Econo and Warrush (the golden
  match and the shared runtime), Maxima, JavaScript and AINull stay, and so does
  Numbi, the custom-game default AI that most game-starting tests use.
- `editor`: the map editor in `src/map/editor/`, apart from its keyboard
  shortcut table, which the settings screen edits, and the brush swatches that
  `glob2 map` renders. `Game_editor.cpp` stays: it holds `Game`'s own placement
  and script queries. The menu's editor button then only prints why it does
  nothing.

The registries keep every id and catalog position, and drop only the entries
that are not built. Creating or loading an AI that was left out fails with
"… is not compiled into this build (compile_profile=core); rebuild with
compile_profile=full". Saves, replays and scripts keep the same ids. Lobby
pickers offer only the AIs and generators that were built, and the custom-game
lobby replaces a saved AI that was left out with Nicowar, as does loading a saved
network game, whose remote players normally become Castor.
Rated play, `match verify` and `online turn-client` refuse to run outside the
full profile. `glob2 info version` reports `compile_profile`, and the build
directory's `profile.json` records it.

Core builds go in their own directory (`…-compile_profile-core`), so full and
core objects never mix. Only `src/app/CompileProfile.cpp` and the AI and
generator registries include the generated `glob2/CompileProfile.h`. No compile
line gains a define, so the other objects match the full build's. The first
core build still recompiles about half of them: a source that includes a header
generated in the build directory (such as `glob2/BuildConfig.h`) carries that
directory's path in its preprocessed text, so it misses the compiler-cache entry
the full build made. Later core builds are incremental as usual. Editing
`src/game/Game.h` recompiles about 20% fewer client sources in the core profile
(219 instead of 274), and `src/unit/Unit.h` about 22% fewer. Test registry
entries name the groups they need with `require={'generators'}`, `'editor'` or
`'ai:castor'`, and core test binaries leave those entries out. A single case
that needs one takes `* glob2test::needs({"ai:castor"})` (`test/support/ProfileNeeds.h`)
and is not listed in a core build; loops over several AIs skip the ones
`AIRegistry::available` rejects.

The full profile is the default. CI, releases and every perf build use it, and
final PR verification must use it too: a passing core build covers only the
code it compiled.

## Build tiers

| Tier | Command | Evidence it may back |
| --- | --- | --- |
| dev | `python3 tools/dev_build.py` (`-O0 -g1`; CMake + Ninja on Linux) | correctness, checksum and golden traces, save/replay continuity |
| perf-screen | `scons perf=1` (`-O2 -g1`) | the direction and rough size of a performance change |
| perf-confirm | `scons perf=1 opt=3` (`-O3 -g1`) | the number quoted in a performance PR; required when screening shows under 5% or mixed signs |
| release | `scons release=1` and the platform packaging | shipping, package size and store QA only; never PR performance evidence |

`perf=1` is a native client build in `build/<toolchain>/client/perf` (`perf/o3`
with `opt=3`). It keeps the full client profile, lossless development assets and
`-g1` symbols, and does not strip or use `-fwhole-program`, so a profiler can
attribute samples and every performance claim is reproducible from one `scons`
command. `tools/dev_build.py perf=1` runs it with the managed compiler cache;
the perf tiers always build with SCons, like the
[master baselines](#comparing-against-master) they are compared with, so a
candidate and its baseline share one build system's compile and link lines.
Rendering measurements that need shipped asset paths add
`sprite_packing=video optimized_assets=1`. Simulation numeric sources keep their
strict flags in every tier, so the dev and perf builds of one platform produce the
same simulation checksums; a mismatch is a bug, not a reason to bump the
simulation revision.

`pch=1` precompiles
### Comparing against master

`python3 tools/dev_baseline.py build` builds the dev and perf tiers of
origin/master in a detached worktree under the development store; with the shared
compiler cache, a baseline one merge newer than the last is mostly cache hits.
`tools/perf_ab.py` then measures a candidate build against it in alternating
pairs and prints the table for the pull request:

```sh
python3 tools/dev_build.py perf=1 engine-tests
python3 tools/perf_ab.py --baseline master --candidate build/linux/client/perf --name terrain-hazard \
  -- '{build}/test/glob2-engine-tests' -ts=TerrainHazardBenchmark '-tc=*idle decisions*'
```

`--baseline master` resolves the baseline of the branch's merge base
(`dev_baseline.py path --merge-base HEAD`); any build directory works too. The
metric is wall time unless `--metric REGEX` picks a number the benchmark prints.
Screening uses three pairs; quoted numbers use `perf=1 opt=3` and seven pairs.
For those, build the master baseline with `dev_baseline.py build --variants
perf-o3`, which `--baseline master` then selects for an `opt=3` candidate, as
does `tools/inline_guard.py`. The change it reports is the median of the
per-pair changes, with their interquartile range, so load from other work on a
shared machine lands on both halves of a pair rather than deciding the result.
On a shared machine, also pin with `--pin [CPUS]` (taskset, by default the last
CPU), and prefer a CPU-time metric the benchmark prints (for example `/usr/bin/time
-f cpu=%U` with `--metric 'cpu=([0-9.]+)'`) over wall time. `dev_baseline.py gc --keep 3` removes
older baselines that no comparison is using, and `dev_baseline.py seed WORKTREE`
copies a baseline's dev build into a fresh worktree so its first build recompiles
only what differs. Baselines build with SCons (`--generator=scons`) in every
variant: the perf tiers are SCons-only, so this keeps a candidate and its
baseline on the same build system, and `seed` therefore fills the SCons dev
directory, which helps only `--generator=scons` builds.
 stable standard headers separately for each compiler/flag
configuration. Its C++ objects bypass ccache rather than weakening cache
correctness; C and Objective-C++ objects bypass PCH. `unity=1` combines only the
checked-in allowlist in deterministic groups of at most four files, preserves
ordinary source commands for IDEs, and reuses production objects in harnesses.
Both experiments default off; their speed benefit must be measured. Precompiling
a wider standard set (adding `chrono`, `functional`, `iostream`, `filesystem`,
`format`, `nlohmann/json.hpp` and `SDL3/SDL.h`) cut `-O0` compile CPU only to 0.93×
on a random 16-file sample on Linux GCC 15, and made small files slower to compile
than without it; PCH is worth making a default only below about 0.75×, so it stays
opt-in. Most remaining compile time is the project's own headers, which the
[header dependency rules](conventions.md#header-dependencies) address instead.

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
HTTP responses in up to three rounds, with one- and two-second delays between
rounds. Each round tries the primary URL and any locked mirrors in order; a
transient primary failure falls through to a mirror without an extra delay.
Every source must match the same locked SHA-256 before installation; checksum
failures and permanent HTTP errors fail immediately.
Explicit
prefix overrides and `GLOB2_DEV_MODE=isolated` remain supported. Clean, dry-run,
help and compilation-database-only requests do not provision dependencies.
A cold native dry-run can still require an existing SCons configure directory.

### Sharing the compiler cache between checkouts

Every checkout of the same commit produces the same compiler-cache keys, so a new
worktree compiles mostly from direct cache hits. Nothing checkout-specific reaches
a compile command line or a widely included header: the absolute source, runtime
asset and renderer shader directories live in the generated
`glob2/BuildPaths.h`, which only the four files that use them include, and the
version banner reports the source revision from `glob2/BuildStamp.h` instead of
`__DATE__`/`__TIME__`. Add a new checkout-specific value to
`buildsys/checkout_paths.py`, never to `BuildConfig.h` or a `-D` flag.

Builds other than shipping invocations (`bundle`, `package`, `install`,
`retain_symbols`) set `CCACHE_BASEDIR` to the checkout and map debug paths to
`.`; run the debugger from the checkout or set its source substitution directory
when debugging elsewhere (`settings set target.source-map . <checkout>` in LLDB,
`set substitute-path . <checkout>` in GDB). Runtime paths and `__FILE__` semantics
are preserved. Shipping invocations keep absolute debug paths.

The managed cache has one configuration file, `ccache.conf` in its directory,
written by every build: a 20 GiB `max_size`, `compiler_check = content` and
compression. Builds and `tools/dev_build.py --cache-stats` read the same limit;
`CCACHE_MAXSIZE` or other `CCACHE_*` variables still override it for one run, and
an explicit `CCACHE_DIR` uses that directory's own configuration.

### Parallel lanes

Every build, test run and asset export on one machine shares a pool of `ncpu + 1`
CPU tokens (`buildsys/jobserver.py`): each compiler or linker process SCons spawns,
each running test process and each asset-encoding worker holds one token, so
several checkouts building at once share the CPUs instead of multiplying the
process count. `-j` keeps its meaning as a lane's maximum. Tokens are `flock`ed
files under the development store's `state/jobserver/`, released by the kernel
when a process exits, so a killed build leaks nothing. Children of a token holder
acquire nothing. `GLOB2_JOBSERVER=0` disables the pool and `GLOB2_JOBSERVER_TOKENS`
resizes it; Windows builds run without it. `python3 tools/dev_environment.py
lanes` lists the builds running now.
### Up-to-date checks

SCons rehashes a file only when its timestamp changed (`Decider('content-timestamp')`),
and every build except `release=1` keeps the scanned `#include` list of each file
until that file changes (`implicit_cache`). Together they take a no-change
`tools/dev_build.py . engine-tests unit-tests` from about 68 s to about 27 s on a
32-core Linux host. After adding a header that shadows another one on the include
path, or moving include directories, run one build with `--implicit-deps-changed`.

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

### CMake + Ninja build for daily work

On native Linux, a CMake + Ninja build of the dev tier is the default for the
everyday edit, build and test loop. It compiles the same sources with the same
flags (`-O0 -g1`, the managed compiler cache, every feature, and
`-fno-fast-math -ffp-contract=off` on the strict-numeric simulation sources),
and its glob2 simulates what the SCons dev build does. What it saves is the fixed
cost of each build: SCons spends about 26 s on a 32-core host starting up and
scanning dependencies before it compiles anything, where Ninja's no-change build
takes about 1.5 s, and the links run as soon as their inputs are ready.

`tools/dev_build.py` uses it whenever the request is only CMake targets, `.`,
`-k` and `-jN` (see [fast development builds](#fast-development-builds)).
**SCons remains the only release, packaging, perf (`perf=1`), browser, mobile,
macOS and Windows build**, and the build for any SCons option. Use SCons for
anything that ships, for performance numbers you quote, and whenever a guide
names a SCons command; `--generator=scons` gives the SCons dev build of the
same targets. The CMake build needs CMake 3.25 or later, Ninja and
`pkg-config`; if the host lacks CMake or Ninja, `pip install cmake ninja` into a
development virtual environment provides both (without them the wrapper falls
back to SCons).

```sh
python3 tools/dev_build.py .                            # glob2 and the four test binaries
python3 tools/dev_build.py                              # glob2 only
python3 tools/dev_build.py unit-tests map-generator-golden-test -j12
python3 test/run_tests.py --binary all -j12             # finds the newest build, build/cmake/dev included
python3 test/test_cli_smoke.py --binary build/cmake/dev/src/glob2
cmake --build build/cmake/dev --target glob2           # the same, without the wrapper
python3 tools/compare_build_systems.py                  # parity report
```

The wrapper configures `build/cmake/dev` the first time; after that Ninja
re-runs CMake itself when `CMakeLists.txt`, `cmake/` or the SCons generators it
imports change. Its arguments are CMake targets (the SCons alias names:
`engine-tests`, `unit-tests`, `map-tests`, `sim-tests`, `tests`, `glob2-tools`,
the harness aliases), `.` for glob2 plus the four test binaries, `-k` to keep
going after an error and `-jN`. SCons options such as `perf=1` or
`compile_profile=core` send the request to SCons (with `--generator=cmake`
they are rejected): those builds exist only in SCons. Test tools that look for
the newest build directory (`tools/build_paths.py`, so `test/run_tests.py`
without `--build-dir`) consider `build/cmake/dev` beside the SCons dev and
release builds and take whichever was built last; `--build-dir` or
`GLOB2_BUILD_DIR` picks one explicitly.

Keep both build systems' inputs in step: after changing `buildsys/sources.py`,
`buildsys/javascript.py`, `test/tests.py` or `buildsys/harness_programs.py`, run
`python3 buildsys/export_cmake_sources.py` and commit `cmake/sources.cmake` and
`cmake/tests.cmake` (`--check` reports a stale file). A new source file needs
nothing more; a new build step or generator needs a matching rule in
`CMakeLists.txt`, checked with `tools/compare_build_systems.py`.

The `dev` preset (`CMakePresets.json`) builds into `build/cmake/dev` with
`-O0 -g1 -fdebug-prefix-map=<checkout>=.` and writes `compile_commands.json`.
When ccache is installed it is the compiler launcher, with the managed cache
directory and settings SCons uses (the two builds rarely share entries, since
CMake passes absolute source paths). Like SCons' `linker=auto`, it links with `lld` when
`ld.lld` is installed (`-DGLOB2_LINKER=default` turns that off). Ninja does not
draw from the SCons [token pool](#parallel-lanes); the wrapper passes `-j8`
unless given `-jN`.

What it builds:

- `libusl.a`, `libgag.a`, `libGlob2JavaScript.a` (QuickJS and openlibm with the
  strict numeric flags of `buildsys/javascript.py`, applied per source), every
  client object, as the object library `glob2_client`, and `glob2`. The numeric
  symbol guard runs once the guarded client objects are compiled, before any
  program links them.
- `glob2-engine-tests`, `glob2-unit-tests`, `glob2-map-tests` and
  `glob2-sim-tests` from the `test/tests.py` registry, as `test/SConscript`
  builds them: the test include directories and `SDL_MAIN_HANDLED`, one object
  library per group of entries with the same `cxxflags`, `defines` and `require`
  (so `-fno-access-control` entries and the `GLOB2_ORDER_CODEC_ONLY` codec
  objects compile separately, the latter once for every binary that links
  them), the client objects reused wherever SCons reuses them, the `glob2_map`
  and `glob2_sim` module archives linked as one `--start-group` group, and a
  `<binary>.provenance.json` sidecar written by `buildsys/test_provenance.py` on
  every build of the alias.
- The `test/tests.py` `PROGRAMS` and the `src/SConscript` harnesses listed in
  `buildsys/harness_programs.py` (now the table SCons builds them from too):
  `MapGeneratorGoldenTest`, `LANSessionHarness`, the `transport-test`
  programs, `OnlinePlayHarness`, `mobile-gallery` and the skin tools.
- The runtime assets: the `buildsys/runtime_assets.py` export, shared through the
  development store, into `build/cmake/dev/runtime-assets`, which `BuildPaths.h`
  names. A depfile lists every asset input and the directories holding them,
  so the export runs again only when one changes. `identity.json` is the SCons
  dev-tier identity plus `"generator": "cmake"`, which `test/run_tests.py`
  reads for its timeout scale and SCons refuses to adopt.

Tests, programs and harnesses are not part of the default build, as in SCons;
the SCons aliases (`engine-tests`, `unit-tests`, `tests`, `glob2-tools`,
`transport-test`, `map-generator-golden-test`, the legacy per-harness aliases
and the rest) are CMake targets of the same names.

Source lists, include directories, the strict-numeric source sets, the test
registry and the harness table come from `cmake/sources.cmake` and
`cmake/tests.cmake`, which `python3 buildsys/export_cmake_sources.py` writes from
`buildsys/sources.py`, `buildsys/javascript.py`, `test/tests.py` and
`buildsys/harness_programs.py`. Commit the regenerated files with any change to
those; `test/build_system/test_cmake_sources.py` fails while one is stale.
`BuildConfig.h`, `BuildPaths.h`, `BuildStamp.h`, `SkinMaterials.h`, the
renderer shaders, the Hive contract, the online error catalog and
`SkinRenderRecipe.h` come from the SCons generators, through
`buildsys/cmake_generate.py`.

What it does not do yet: build the pinned recording libraries (it uses
`GLOB2_SDL3_PREFIX`'s `recording/` or the SCons build's prefix), the relay and
its tests, installation and packaging, the perf (`perf=1`) and release tiers,
PCH or unity variants, or any browser, mobile, macOS or Windows target.
`-DGLOB2_EXPORT_RUNTIME_ASSETS=OFF` skips the asset export and points
`BuildPaths.h` at `GLOB2_RUNTIME_ASSET_DIR` (default: the SCons dev build's).

`tools/compare_build_systems.py` builds glob2, the four test binaries, the test
programs and the harnesses with both systems into `build/parity/scons` and
`build/parity/cmake`, both with an extra `-ffile-prefix-map=<checkout>/=`
(`--glob2-only` limits it to glob2). It then compares every compilation in the
two `compile_commands.json` files: the `-I` search order and the multiset of
all other flags, with paths normalised. A source compiled more than once (test
support in both binaries, a production source compiled again with test
defines) must have the same multiset of compilations in both. Matching
compilations' objects are compared by sha256, raw and with debug sections
stripped. The CMake parity build compiles through ccache with
`-DGLOB2_CCACHE_BASEDIR=` (empty): `CCACHE_BASEDIR` rewrites CMake's absolute
source paths relative to its build directory, so in the ordinary preset
`__FILE__` reads `../../../src/...` where SCons has `src/...`, and objects that
use it differ in that string. `--no-build` reuses existing parity builds,
`--time-noop` also times a no-change rebuild of both ordinary dev builds,
`--json` writes the full report and `--summary` appends a Markdown summary. It
exits 1 on any flag or source-set difference; object differences are reported
only.

Remaining object differences: every object's debug sections differ (the
compilations run from different working directories), and four objects differ
in data: three embed `BuildPaths.h`'s absolute build-directory paths, and
`TextSort.cpp`'s `assert` text names `third_party/natsort/strnatcmp.c` where
SCons, finding it through `-I.`, has `./third_party/...`. In the ordinary
(non-parity) CMake build, the `__FILE__` difference above also reaches
doctest's case file names (`../../../src/...`); `test/run_tests.py` maps them
back to repository paths (`source_name`), so the module case contract, JUnit
class names and results match the SCons build.

The `build-system equivalence` workflow
(`.github/workflows/build-system-equivalence.yml`) runs that comparison with
GCC 13 on Ubuntu 24.04 on master pushes and on pull requests that touch
`CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `buildsys/`, `SConstruct`, any
`SConscript`, `test/tests.py` or the comparison script. It fails on a flag or
source-set difference and writes object-hash results to the step summary and
the `build-system-equivalence` artifact without failing on them until parity
is exact.

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

Apple packaging fetches hash-pinned MoltenVK **1.4.2** using `buildsys/moltenvk.json`.
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
