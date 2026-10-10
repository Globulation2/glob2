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

Use `GLOB2_DEV_MODE=isolated` for checkout-local installations and caches with
vcpkg binary caching disabled. CI release verification selects this mode
explicitly. Run shared-store pruning and migration in shared mode; those commands
reject isolated mode to preserve the shared store's locking boundary.
No setup command accepts new Android SDK licenses automatically;
run `python3 tools/dev_environment.py sdkmanager -- --licenses` yourself before installing SDK
platform/build packages with `mobile/setup_tools.py --sdk-packages`.

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
unused-resource cleanup, and opted-in ccache builds default to a 4 GiB ceiling.
The development command requests a 12 GiB compiler-cache ceiling unless
`CCACHE_MAXSIZE` is set. If active resources prevent meeting the aggregate budget,
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
