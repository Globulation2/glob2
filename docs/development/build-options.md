# Development build options

Fast builds accelerate editing; ordinary debug builds provide full symbols and release builds provide representative runtime behavior.

## Fast development builds

`python3 tools/dev_build.py` enables caching and `dev_fast=1` in a separate output
directory, forwarding SCons targets and options. It defaults to at most eight
jobs and `linker=auto`. Existing `scons` defaults and release flags are unchanged.
Install ccache before using the development command. Native dependencies still
come from the existing package-manager or `GLOB2_SDL3_PREFIX` configuration;
mobile dependencies and SDKs still require their existing setup steps.

```sh
python3 tools/dev_build.py
python3 tools/dev_build.py engine-tests -j8
python3 tools/dev_build.py dev_fast=0 linker=default
python3 tools/dev_build.py pch=1 unity=1
python3 tools/dev_build.py target=web web_variant=serial
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
