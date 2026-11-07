# Shared development storage

Manage downloaded SDKs, dependencies and compiler caches without sharing mutable application output across checkouts.

## Shared development storage

Normal development shares pinned mobile tools, Emscripten installations, and
checksum-verified mobile dependency bundles across checkouts. Object files,
generated configuration, application packages, signing keys, temporary files,
and writable Android/iOS simulator state remain checkout-local. Native libraries
continue to come from the platform package manager; `CCACHE=1` remains opt-in.
An explicit `CCACHE_DIR` or SDK/dependency argument retains its existing override.

`GLOB2_DEV_HOME` selects the store. Defaults are
`~/Library/Application Support/Glob2/Development` on macOS,
`$XDG_DATA_HOME/glob2/development` (or `~/.local/share/glob2/development`) on Linux,
and `%LOCALAPPDATA%/Glob2/Development` on Windows. Installations are namespaced by
host and pinned manifest contents, so different branches can use different
versions concurrently. Identical downloaded components (for example, the same
NDK used by different Android API configurations) are stored once and linked into
each toolchain bundle. On POSIX hosts, dependency builders use private, stable
space-free aliases when upstream makefiles cannot quote an SDK path containing
spaces; the tools still live in the selected store. The SDK Java wrapper likewise
passes arguments directly instead of relying on upstream shell launchers.
Setup verifies downloaded archives and publishes tools
atomically. Dependency bundles include compiler fingerprints and verified
libraries, plus pinned SDL Java sources for Android packaging.

Native and browser builds also share their runtime-asset export. The store keeps
one finished export per input key in `caches/runtime-assets/<key>/` (the key
covers the exported files' Git blob ids or content hashes, the export tools and
the export options), and a build whose key is present copies it into its build
directory instead of re-encoding: copy-on-write on APFS, XFS and Btrfs, a plain
copy elsewhere, never hard links, so builds cannot alter a stored tree. The
encoded-image cache that exports reuse between different inputs lives in
`caches/asset-cache/`. Both are ordinary prunable cache entries.

Use `GLOB2_DEV_MODE=isolated` for checkout-local installations and caches with
vcpkg binary caching disabled. CI release verification selects this mode
explicitly. Run shared-store pruning and migration in shared mode; those commands
reject isolated mode to preserve the shared store's locking boundary.
No setup command accepts new Android SDK licenses automatically;
run `python3 tools/dev_environment.py sdkmanager -- --licenses` yourself before installing SDK
platform/build packages with `platforms/mobile/setup_tools.py --sdk-packages`.

```sh
python3 tools/dev_environment.py paths --report-file
python3 tools/dev_environment.py paths --field android_sdk
python3 tools/dev_environment.py status
python3 tools/dev_environment.py prune --dry-run
python3 tools/dev_environment.py prune
python3 tools/dev_environment.py migrate --dry-run
python3 tools/dev_environment.py migrate --apply
```

Pruning is explicit through `tools/dev_environment.py prune`; builds do not evict
managed store entries automatically. The default ceiling is 32 GiB for regenerable
caches and completed dependency bundles, configurable with a positive integer
`GLOB2_DEV_BUDGET_GIB`. Cleanup evicts least-recently-used idle entries;
active entries are protected by OS-held leases. Gradle uses its native seven-day
unused-resource cleanup. The managed compiler cache keeps a 20 GiB ceiling in its
own `ccache.conf` ([build options](build-options.md#sharing-the-compiler-cache-between-checkouts));
`CCACHE_MAXSIZE` overrides it. If active resources prevent meeting the aggregate budget,
cleanup reports the remaining pressure; repeat explicit pruning when idle. Installed toolchains, external
override caches, checkout build outputs, simulator data, and `artifacts/` are
outside managed pruning. Status reports their sizes separately.

Migration inventories registered retained checkouts and seeds the store only
from checksum-verified archives. Busy checkouts can supply verified archives but
must not have their tool directories removed. Validate representative shared
builds before replacing any duplicate tool installations. `migrate --apply
--validation receipt.json` additionally replaces verified duplicate directories
with compatibility links, retaining directories whose contents differ from the
verified installation. It removes duplicate checksum-verified download archives
after seeding the shared cache; those archives can be downloaded again if needed.
The receipt records `tools`, `toolchain_key`, and zero
exit codes for `checks.android-packaging` and `checks.build-system-tests`; it must
refer to the installed toolchain being migrated. Only components with the same archive proof are eligible, so differing tool
versions are left alone while identical components can be reused across Android
API configurations. A receipt may also include `browser_sdk` and a zero exit code
for `checks.browser-build` to replace identical installed Emscripten components.
Browser migration retains the checkout SDK configuration and Git history;
components containing different files or legacy caches stay local. Older scripts that include installation paths in compiler
fingerprints may require a one-time dependency rebuild after linking. Preserve simulator
state and signing material. Older checkouts may still reference their local tool
paths and need compatibility links or updated scripts before cleanup. Migration
never infers that extracted tool directories are trustworthy from a version
string alone. Windows inventory/seeding is supported; duplicate removal requires
a supported open-file checker and is conservatively skipped there.


## Reclaiming space

Checkouts, not the managed store, hold most development disk: build directories,
`artifacts/` and finished worktrees. These commands report and reclaim them:

```sh
python3 tools/dev_environment.py disk                  # largest consumers first, and free space
python3 tools/dev_environment.py worktrees             # size, idle days, branch, PR state
python3 tools/dev_environment.py worktrees --finished  # merged or closed, clean and unlocked
python3 tools/dev_environment.py worktrees --remove PATH...
python3 tools/dev_environment.py builds --gc --idle 14 --dry-run
python3 tools/check_artifacts.py [--fix]               # raw images, copied build trees, large logs
```

`worktrees` reads pull request state through `gh`, so squash-merged branches,
which `git branch --merged` cannot see, still count as merged. `--remove` refuses
the main checkout, locked trees, trees a process or build is using, and trees with
uncommitted changes unless `--discard`; it asks before each removal unless `--yes`.
Every build touches `.last-use` in its build directory, and `builds --gc` deletes
directories idle for longer than `--idle` days that no build holds. `tools/check_artifacts.py
--fix` gzips logs over 1 MiB and converts `.ppm`/`.bmp` captures to PNG; it never
deletes copied build trees.

Builds, test runs and asset exports stop before filling the disk: they fail when
less than `GLOB2_MIN_FREE_GIB` (default 10) GiB is free, naming these commands.
`GLOB2_MIN_FREE_GIB=0` disables the check.

## Experiment output

Experiment artifacts are outside SDK/cache pruning. Use the bounded
[tournament runner](../tools/tournaments.md#experiment-storage) for broad AI
campaigns. Its default game outputs omit saves and replays; a compact probability
profile records the existing win model without retaining unrelated console
telemetry. Logs are compressed during capture, attempts and collected artifacts
have explicit byte allowances, and disk pressure stops new work before evidence
is lost. Keep exhaustive per-object checksum dumps and frequent checkpoints to
short, selected diagnostic runs. They can exceed the simulation's own storage
by orders of magnitude.

Cleanup must distinguish reusable evidence from duplicated intermediate output.
Retain manifests, seeds, build identity, results and the compact measurements
needed for the analysis. Remove detailed mismatch-debug payloads only after
verifying retained checksums and original match artifacts, and record the removal.
Do not infer that an ignored `artifacts/` directory is disposable in its entirety.
