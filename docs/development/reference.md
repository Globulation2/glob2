# Development reference

Repository-specific build pitfalls, verification techniques and conventions.
Consult the sections relevant to the change; choose tools and workflow appropriate
to the task. Verify implementation details against current code and update these
notes when the referenced behavior changes.

Tournament CLI, persistent workers, and per-player AI save compatibility are
documented in [Distributed tournaments](../tools/tournaments.md).

## Build and test entry points

Choose build concurrency for available memory and other running builds; CPU count
alone is not a safe job limit. The commands below leave concurrency unspecified.

```sh
scons                        # default client: debug information, no optimization
scons release=1                # optimized client, including headless runs
scons role=relay release=1 relay # the match relay and its tests (docs/multiplayer/relay.md)
scons release=1 package        # macOS signed app bundle and DMG
scons release=1 bundle         # macOS app bundle without the DMG
scons release=1 tests          # glob2-engine-tests and glob2-unit-tests
python3 test/run_tests.py      # run them; --list, --filter, --tag, --shard
```

- The test binaries are built by the same `scons` invocation as the game, from
  the same objects, and listed in `test/tests.py`; a domain's test files sit
  beside the code they test and cross-domain tests in `test/`. `test/README.md`
  describes the runner, the fixtures and where a new test goes. Extend an existing
  relevant suite where practical.
- Options are explicit on each invocation; there is no cross-invocation
  `options.py`. Outputs and generated configuration are isolated under
  `build/<toolchain>/<role>/<mode>`: `darwin`, `linux`, or `mingw` for native
  toolchains; `client` or `relay` for the role. The YOG lobby `server` and
  `router` roles were removed at the M9 cutover: `role=server`, `role=router` and
  `server=1` are refused, and `server=0` is accepted as a no-op so existing client
  commands keep working. Use `release=1` for headless
  measurements: the unoptimized build can be substantially slower. Use `release=0`
  for debugging. `scons -c` cleans; `--build=/tmp/out` selects an out-of-source
  build directory; `BINDIR=/path/bin INSTALLDIR=/path/share` selects installation
  locations.
- `mingw=1` builds natively on Windows; `mingwcross=1` cross-compiles. Dependencies
  are in `vcpkg.json` and CI. Check the affected platform jobs rather than assuming
  a successful local build covers another compiler or operating system.
- The **Steam Windows package** workflow runs manually, as a reusable workflow, or
  from the release workflows; it never runs on pull requests or master pushes, because
  packaging checks are only needed when releasing. The same rule applies to the Mac App
  Store build and to Android release-contract, store-listing and APK checks (pull
  requests only build and smoke-test Android). It builds the MinGW release
  client and stages `glob2.exe`, its runtime DLL dependency closure, game assets,
  license, and attribution in one depot folder. A separate Windows job downloads
  that artifact and runs a short headless game without the build toolchain. Download
  the `glob2-steam-windows-<commit>` artifact from the workflow run; its contents
  are the files to place at the root of a Windows Steam depot. The workflow does
  not upload to Steam or publish a release. The separate **Upload Steam Windows
  depot** workflow is stored here for review and synchronization, but its package
  and upload jobs run only when manually dispatched in the owner-controlled public
  `genixpro/glob2-release` mirror from `master`. Its upload job uses the
  `steam-release` environment, restricted to that branch, and reads Steam
  credentials only there. The `Globulation2/glob2` source repository has no Steam credentials, cannot
  pass credentials into the package workflow, and cannot start an upload job.
  The owner syncs the mirror and dispatches each upload; no push, pull request,
  `workflow_run`, or repository event starts it. The upload creates an unpublished
  SteamPipe build. It does not set a Steam branch live or publish store changes.
  Configure `STEAM_APP_ID`, `STEAM_WINDOWS_DEPOT_ID`, and
  `STEAM_BUILD_USERNAME` as environment variables and
  `STEAM_CONFIG_VDF_BASE64` as an environment secret on the release mirror only.
  The latter is the base64-encoded `config/config.vdf` from a dedicated SteamCMD
  builder account after an interactive Steam Guard login; preserve and refresh it
  if SteamCMD updates the login token. Never place it in a depot, workflow artifact,
  source repository secret, or the source workflow. Steamworks must have a Windows
  depot assigned to the app and its test package, with a `glob2.exe` launch option.
  Check the result interactively on a clean Windows installation before using it
  in Steamworks. Windows gameplay now defaults saves to SDL's per-user preference
  directory when `GLOB2_USER_DATA_DIR` is unset; validate save and load before
  setting a build live.
- `scons target=web release=1` builds the WebAssembly browser client; see
  `docs/browser/adr-001-build-isolation.md` for the toolchain isolation this relies on.
- Dependencies include pinned SDL3/SDL3_net/SDL3_ttf/SDL3_image (see `scons/sdl3-versions.json`) and WebP 1.6.0 for optimized packaged artwork, Opus/opusfile/Ogg, Speex, OpenGL/GLU, libepoxy,
  zlib, fribidi and pcre; PortAudio is optional. Builds without fribidi (Android, iOS, browser) shape and
  order Arabic and Persian text with `libgag/src/BidiText.cpp`, which the `BidiText` unit tests keep identical to
  fribidi on every Arabic and Persian catalog string. All native multiplayer builds (client and relay) require OpenSSL and the header-only
  Boost.Beast and Boost.Asio; nothing else uses Boost. `role=relay` builds only
  `glob2-relay` and its tests and links no SDL library (it still needs SDL3's headers);
  see [the relay guide](../multiplayer/relay.md).
  The source helper and vcpkg overlay apply the same reviewed SDL3 X11 patches:
  an [upstream reparenting fix](https://github.com/libsdl-org/SDL/commit/25f4af8fcf7d1a9a06be8d89694b1c612158f41f)
  backported to 3.4.16, and a mapping wait that accepts confirmed window state
  when its notification is missing. Patch hashes are recorded in the SDK manifest
  and invalidate cached builds. Use this patched SDK for the pinned X11 build.
- `CCACHE=1` opts into the shared compiler cache. Unset it when generating
  `compile_commands.json`; do not add `CCACHE_SLOPPINESS` settings that weaken
  content or time-macro validation (`include_file_mtime`, `include_file_ctime`,
  `time_macros`). `scons/ccache.py` is used by both build entry points; the environment
  opt-in is not persisted in `options_cache.py`. CI keeps one cache per job and, before
  master saves it, drops every entry that run did not use, so the saved cache is the
  working set rather than an accumulation bounded only by `CCACHE_MAXSIZE`.
  The main Linux CI builds also restore SCons objects and configuration probes
  keyed by compiler contents. They rebuild `GlobalContainerArgs.cpp` each run
  because it embeds the compilation date and time.
- `release=1` outside macOS strips binaries (`-s`), so it also drops `-g`: debug
  information there only slowed compilation and multiplied object and cache sizes.
- The public Mac pull-request check compiles with `scons release=1` and runs a
  raw-binary command-line smoke test without release signing. On a manual run
  from the owner-controlled release mirror's `master`, the workflow runs
  `scons release=1 bundle` and then
  `darwin/package_app_store.py`. The script copies the bundle to the ignored
  `artifacts/mac-app-store/Glob2.app`, sets its App Store version and build number,
  adds Retina icon sizes from the existing 128-pixel artwork, applies
  `darwin/AppStore.entitlements`, and verifies its signature. Its App ID defaults
  to the iPhone ID, `org.globulation2.glob2`. When `upload` is selected, the
  release mirror supplies the Mac distribution identities and provisioning
  profile, then makes a signed `.pkg` beside the app. The existing `package`
  target remains the direct distribution DMG path. Test saves, map import, LAN
  hosting and online play in the sandboxed app before upload; local testing
  alone does not establish App Store acceptance. See the
  [Mac App Store release process](mac-app-store.md) for the mirror-only manual
  workflow and required signing credentials.
- Keep harness runs out of personal profiles: use the existing disposable-profile
  runners and retain fixtures, seeds, logs and checksums needed to reproduce a result.
- CI lets independent test steps finish after a failure and records their raw
  outcomes in the job summary. The final summary step fails the job if any
  required check failed. Keep build prerequisites as prerequisites, and use
  `test/ci_run_commands.py` when several independent commands share one step;
  `test/run_tests.py` already isolates and aggregates the native test cases. Artifact uploads run after failed checks
  so reviewers can inspect the available evidence. The Linux client builds once
  per supported toolchain, then distributes its built programs to four parallel
  test shards per toolchain. Linux compiler builds share
  `.github/workflows/ci-linux-build.yml`; the GCC artifact builds and Clang
  compatibility check run separately, so runtime shards do not wait for Clang.
  Primary GCC 13 programs are published before CLI regressions and consumed by
  both native shards and browser transport checks. Each selected GCC toolchain
  starts its own runtime shards without waiting for Clang. The stable
  `Relevant checks passed` summary requires every selected check to succeed, but
  is not a merge prerequisite. Cheap-only success does not establish engine
  verification or acceptance of local PR evidence. Both draft and ready PRs run
  cheap contracts by default; `ci:run` or `ci:full` requests hosted verification.
  The [risk policy and rollout](#tiered-pull-request-coverage-rollout) controls
  secondary platforms for explicitly requested PR verification. Retained master
  pushes always run the full development matrix. Unknown inputs select full
  development coverage. Steam/store release packaging runs only for releases or
  explicit dispatches, including when its helper files change.
- CI run cancellation: superseded PR revisions cancel through server-side
  concurrency. Master runs finish once started and only the newest pending push
  remains; nightly and manual runs have separate groups. Nightly skips expensive
  jobs when available successful full evidence covers its exact master SHA and
  current coverage policy; otherwise it runs the full matrix. Call-only workflows
  inherit their caller's cancellation. Other callable workflows use a literal
  prefix distinct from their caller to prevent deadlocks. The trusted
  `cancel-superseded.yml` workflow uses `pull_request_target`, checks out only
  default-branch code, and cancels allowlisted event-triggered validation left
  behind by updates or closed/merged PRs, including forks. Legacy Steam/App Store
  PR runs from older workflow definitions are retired even at the current PR head. It never executes PR
  code or cancels manual releases. Its sweep can clear obsolete pending master
  pushes but protects master once any job has begun (including between jobs),
  even when the workflow API reports it as queued, and protects scheduled runs. Closed-PR caches and old
  master cache generations are reclaimed on closure and in a daily sweep.

For headless games, use the client binary's `--nox <game-file> <steps> <runs>`
option. `-test-games-nox` runs random AI games indefinitely unless bounded as
explained in [headless replay generation](headless-replays.md). Replays default
to `~/.glob2/replays/last_game.replay`; `GLOB2_REPLAY_PATH` overrides the location.
See `test/README.md` for the Map-subclass test pattern that avoids linking the
full simulation for map-predicate tests.

The client also offers `--generate-map`, `--preview-map`, and
`--list-map-generators` modes (including optional `--json` reports;
[format and metric meanings](../map-generators/REPORT.md)); see [map CLI](../map-generators/CLI.md) for config files,
settings, PNG output, and previews of existing maps and saves. PNG export uses
the shared `MapPreview` widget on an offscreen software surface; all map CLI
outputs can run headlessly. Preview scale defaults to 2×; 4× and 8× are available.

Test runners use the current host’s native release directory. Set
`GLOB2_BUILD_DIR` when using `--build` or a debug build (whose client
role directory is `client-tcp`). Pass explicit binary paths to CLI tools when
comparing builds; do not pick an old binary by modification time.

### Windows Store release

`.github/workflows/windows-store-release.yml` is a manual `workflow_dispatch` on
`windows-2025`. Its sole job is gated to the owner of `genixpro/glob2-release`
on `master`:
the public `Globulation2/glob2` repository stores the workflow and build code for
review, but dispatching it there cannot build, sign, upload or publish a release.
The owner syncs the public changes into the public release mirror and starts each
release there manually. The mirror's `master` tracks public `master` exactly; see
[the release mirror](releasing.md#the-release-mirror). The workflow builds the existing MinGW x64
client, stages its runtime DLLs, game assets and GPL license, creates
`MicrosoftGame.config`, shell logos and a 1920×1080 splash image, then
uses the Microsoft GDK to produce an MSIXVC package. With `upload: false`, it
creates an installable test-signed package and retains the package and validator
report as a publicly accessible GitHub Actions artifact. With `upload: true`, it
creates a submission-encrypted package and sends it with its encryption key blob
directly to an existing Partner Center branch; neither is retained as a public
artifact. Keep credentials out of workflow logs and artifacts.
The workflow does not submit a listing for certification or publish it to retail.

Keep the release mirror public and owner-controlled: disable pull requests,
issues, projects, wiki and discussions; leave `genixpro` as its only
collaborator; restrict creation, updates and deletion of every mirror branch to
that user, while `master` also blocks force pushes and deletion. Restrict Actions
execution to `genixpro`, allow only the pinned actions
needed by the mirror workflows, and keep the default `GITHUB_TOKEN` read-only.
Enable secret scanning and push protection. Public repositories remain readable
and forkable, so never put credentials in code, workflow inputs, logs or
artifacts. Only the mirror's restricted environments hold release credentials.

The individual Windows developer account for Bradley Arsenault is enrolled, and
the [Globulation 2 PC game product](https://partner.microsoft.com/en-US/dashboard/products/9PH4FCRMX19F/setup)
has been reserved in Partner Center. Its package identity and initial package
branch are:

| Partner Center field | Value |
| --- | --- |
| Store ID | `9PH4FCRMX19F` |
| Package Identity Name | `BradleyArsenault.Globulation2` |
| Package Identity Publisher | `CN=EBC9B192-6200-443B-BFEB-9B00B0B78F67` |
| Publisher display name | `Bradley Arsenault` |
| Package branch | `Main` |

The product has not been submitted for certification. On the
**release mirror only**, create a GitHub Actions environment named `windows-store`,
restrict deployment branches to `master`, and set these environment variables
from Partner Center:

| Variable | Value |
| --- | --- |
| `STORE_ID` | 12-character Store ID (also the Package Uploader Big ID) |
| `STORE_IDENTITY_NAME` | Package Identity Name |
| `STORE_PUBLISHER` | Package Identity Publisher, including `CN=` |
| `STORE_PUBLISHER_DISPLAY_NAME` | Publisher display name |
| `STORE_BRANCH` | Existing Partner Center branch for package upload |

For upload, associate a Microsoft Entra tenant with this Partner Center developer
account. The first release tenant may be temporary. Register an Entra application
in that tenant, grant it **Publishing: Read/Write** for this product,
and add its tenant ID, application ID and client secret as environment secrets named
`STORE_TENANT_ID`, `STORE_CLIENT_ID`, and `STORE_CLIENT_SECRET`.
The `STORE_BRANCH` variable is required for upload. Never add these secrets,
the environment or a privileged trigger to `Globulation2/glob2`.
The mirror's `windows-store` environment allows deployments only from `master`
and requires approval from `genixpro` before the job can access its variables
and secrets. Allow the dispatching owner to approve because that account is
the sole reviewer.

After setting or rotating credentials, run **Actions → Check Windows Store
credentials → Run workflow** on the mirror's `master` branch. Supply the
Application (client) ID and Directory (tenant) ID from the Entra app overview;
the secret's ID is not the application ID. This manual check uses the same
restricted environment, rejects accidental whitespace, compares the stored IDs,
and requests a token for PackageUploader's API resource. It logs neither secrets
nor tokens and does not upload or publish packages. A passing check verifies
authentication; the product's publishing permissions are checked during upload.

When a permanent organization tenant is ready, associate it with the same Partner
Center account, register a new product-scoped publishing application there, and
replace the three `STORE_*` upload secrets in the release mirror environment.
Verify an upload with the new credentials before revoking the temporary
application and removing its Partner Center tenant association. This is a
credential and Partner Center association change, not a tenant transfer. The
Store product identity above stays with the Partner Center product.

After syncing the mirror, run the workflow from its **Actions → Windows Store
release → Run workflow** page on `master`, with a
four-part package version greater than previous uploads, with the fourth part
(revision) set to `0` as required by GDK PC packaging; for example, use
`1.0.1.0` after `1.0.0.0`. Leave `upload` false for
the first run. Download the artifact and install the MSIXVC on a clean Windows PC
with Gaming Services; verify launch, sound, saves, settings, uninstall and a
subsequent version update. Windows saves and preferences default to SDL's per-user
preference directory, while bundled data remains read from the installation
directory. After the package passes installation testing, rerun with `upload` true
and advance the Partner Center submission through its listing, age rating and
certification steps. Keep the corresponding GPL source available with each
distributed version.

The Store package uses original MSIXVC, supported by the currently released
GDK. A future MSIXVC2 migration needs its own Partner Center package branch.
See Microsoft's [PC packaging guide](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/overviews/packaging-getting-started-for-pc),
[MakePkg reference](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/deployment/makepkg),
and [Package Uploader setup](https://github.com/microsoft/PackageUploader).

## Release asset and bundle sizes

Release packagers share `tools/package_assets.py`. Original artwork stays in
`data/` and `datasrc/`; generated runtime trees and per-image caches stay under
`build/`. Runtime artwork is WebP in every client build: units, buildings,
terrain, resources, effects, UI, icons, wordmarks and browser menu art. Release
bundles choose the smaller of lossless WebP Q75/method 4 and lossy WebP
Q90/method 6. Both candidates use `exact=True`; lossy explicitly uses
`lossless=False`. Dimensions and decoded alpha must match for every candidate.
Lossless candidates also require full RGBA equality, including RGB beneath
transparent pixels. Higher-depth RGBA PNG sources use the renderer's 8-bit RGBA
representation (rounded normalized 16-bit channels, as SDL converts RGBA64) for
both candidates. Unsupported higher-depth modes fail the export without
quantization; their source artwork remains untouched. Fonts, meshes, music and
platform icon containers are outside this policy.

Source PNG artwork stays in the repository. SCons prepares lossless WebP for
normal source/debug builds and exposes that generated tree to the client and
native test programs. Release builds select the smaller permitted WebP encoding.
Image lookup searches existing directories in order using the exact WebP name;
custom artwork overrides must therefore use `.webp`. Theme backdrop and wordmark
paths also use WebP. Sprite frames, sheets, and HD atlas loaders decode only WebP;
there is no PNG fallback for bundled artwork. PNG/JPEG decoding remains available
for end-user imports and screenshot tools. Online previews and skin textures use
WebP wire renditions (see the platform architecture guide).

`Toolkit::assets()` owns the shared asset pipeline. Requests deduplicate by source
generation, type and preparation mode. Independent reader workers overlap native
filesystem reads; CPU workers decode WebP directly to ARGB8888, cut sheets, build
native atlases and prepare upload pixels and alpha-weighted mip chains. Fonts
share immutable source bytes but open their SDL_ttf objects on the owner thread.
Mesh parsing and stereo Opus stream preparation use the same scheduler. Mutable
music playback cursors belong to the dedicated music producer. GPU creation/upload,
SDL renderer textures and live object publication run on the owner thread. Workers never wait on child
jobs or call renderer APIs.

Use `requestSprite`/`findSprite` and `pollAssets` for asynchronous families;
`SpriteLoad` publishes only complete prepared sprites. Existing synchronous APIs
are adapters over the same service. `AssetLoader::onReady` associates an owner
lifetime with a completion; destroyed screens suppress publication. Independent
subscriptions support cancellation without cancelling other consumers. Handle
copies share cancellation; use `handle.retain()` for an independent subscription
when a continuation captures inputs owned by its caller. Cancelling the caller
then leaves those inputs valid until preparation finishes. New source
mounts and pack changes invalidate future requests while existing consumers retain
valid data. HD reloads prepare on workers and publish during frame polling.
Polling shares one deadline across cooperative work, sprite adoption and HD reloads;
a single decode or upload remains indivisible and can exceed that deadline.
Startup update turns advance preparation separately from progress painting; the
progress view follows the render FPS setting and browser animation-frame callbacks.
Browser visibility or context loss pauses publication until graphics are usable.
Required startup families finish preparation and texture upload before menu entry;
optional browser packages still become visible only after atomic installation.

CPU concurrency defaults to available logical CPUs minus one, clamped to one
through eight workers. Small-image workloads lose throughput to queue contention
and memory traffic at higher counts; the worker override supports hardware tuning.
There are two reader workers on desktop and one on mobile. Browser MEMFS reads
stay on its application host while pthread builds prepare on workers. Serial
browser builds and thread-creation failures use the same dependency queue
cooperatively. Browser package transfers have a shared four-part limit, deduplicate
in-flight package requests and install packages in request order.

`GLOB2_ASSET_THREADS=0` selects cooperative execution; positive values override
CPU workers. `GLOB2_ASSET_IO_THREADS` overrides native reader count.
`GLOB2_ASSET_MEMORY_MB` overrides the scratch admission budget. Automatic admission
uses one eighth of reported RAM, clamped to 64–512 MiB on desktop and 64–128 MiB on
mobile/browser. Compressed image inputs have a separate queue limit within that
budget. Cooperative reads yield when credits are unavailable. One oversized job
may run alone to avoid starvation. The budget estimates transient working memory;
it is not a cap on required decoded asset residency, font source residency, GPU
storage, codec internals or process RSS. `AssetLoader::metrics()` exposes worker
occupancy, admitted scratch, buffered inputs, jobs and cumulative stage times.

For new asset types, submit immutable inputs and dependency continuations through
this service, estimate preparation scratch, and finalize on the owner thread.
Estimation runs under the scheduler lock: keep it quick and do not call service
APIs from it. Sprite estimates include padded atlas cells at the maximum frame
dimensions and any upload copies. Expired weak cache keys are periodically removed
so unique preview and music requests do not accumulate session metadata.
Never wait inside a worker or capture a mutable screen, FileManager or renderer.
Build `scons asset-loading-benchmark` and run the resulting tool against the same
runtime tree with different worker overrides. It reports wall time to final
readiness, worker occupancy, estimated peak scratch and a native base-layer pixel fingerprint.
`GLOB2_ASSET_BENCHMARK_GPU=1` includes OpenGL uploads;
`GLOB2_ASSET_BENCHMARK_HD=1` includes installed HD artwork. Use an external RSS
measurement and distinguish warm filesystem cache from cold reads.

Optimized exports also pack the frames of the sprites in `SPRITE_SHEETS`
(currently `data/gfx/unit`, 2,816 files) into sprite sheets: runs of up to 256
consecutive frames of one layer and size, sixteen tiles to a row, each encoded like
any other image. `<name>.sheet` beside them lists each sheet's file, layer
(`image` or `rotated`), first frame, frame count and tile size. When that index
exists, `GAGCore::Sprite::load` cuts the tiles out of the sheets and ignores the
per-frame files; without one, or if any sheet does not match it, the sprite loads
one WebP file per frame. The exporter checks exact tile placement
before encoding (including cached sheets) and exact per-frame alpha after
decoding; lossless sheets also retain full RGBA. The audit lists each sheet's frames under
`packed_from` so client installs can remove per-frame copies left by older
installs. Opening thousands of small files dominated unit-sprite loading.
HD terrain/resource atlases also have exact source frame placement verified before
encoding; the renderer checks their exact alpha against decoded HD frames, allowing
independently encoded lossy RGB to differ.

Packaging bootstraps a private Pillow 12.2.0/libwebp 1.6.0 encoder environment
when the current Python lacks the pinned encoder. This is a build dependency,
never application content. It requires network access on first setup; subsequent
exports reuse cached verified conversions. Image recipe identity includes the
recipe version, encoder pins, Q90 quality/method and depth conversion policy;
older recipes regenerate automatically. Corrupt cache entries regenerate from
source artwork. Export/install ownership remains `runtime-assets-v1`, so older
generated trees can be replaced and obsolete managed files removed safely.
Flatpak supplies checksum-pinned encoder sources and build dependencies for its offline sandbox. RPM uses
`tools/build_asset_encoder.py` with checksum-pinned Source archives; the helper
builds a private encoder with pip's `--no-index --no-build-isolation` options.
Fetch sources before entering an offline build with
`python3 tools/build_asset_encoder.py fetch --sources <source-directory>`.
Distro installs can request `optimized_assets=1` independently of `release=0`,
preserving distro compiler flags and debug information. `optimized_assets=0`
selects lossless WebP for comparison/rollback; `auto` follows `release`.
The pinned SDL PNG fallback rounds normalized 16-bit channels to renderer
bytes, matching the native libpng decoder and exporter reference.
Windows CI uses standard CPython for encoding and MinGW Python for building;
`GLOB2_ASSET_ENCODER_PYTHON` selects a validated, already prepared interpreter.
Python tests can use the same environment:

```sh
"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s test/build_system -v
python3 tools/package_assets.py --platform linux --output build/runtime-assets
```

The export audit is beside the generated tree, outside shipped assets. Build
helpers and the HD source provenance manifest are omitted, while `frames.txt`,
font coverage, notices, music and all HD images are retained. Linux's public
AppStream screenshot remains original PNG because external metadata references
it. Other platforms omit store screenshots. Android's installed asset
index hashes the exported bytes. Run source `dist` and release `install` as
separate SCons invocations; the latter installs the exported runtime tree.
Client installs retain a compact compressed ownership index to remove obsolete
managed files on upgrades. Unrelated files and modified obsolete files are kept.
On the first upgrade from an install without that index, PNGs at current shipped
image paths are removed when replaced by WebP, including artwork from older
releases. Keep custom image overrides in the user profile so they retain priority.

Windows distributions stage assets and recursively imported DLLs through
`tools/release/windows_runtime.py`. ZIP, Steam, Epic, Store and NSIS use this
shared policy; missing non-system imports fail packaging. `scons release=1
mingw=1 lean_images=1 windows-installer` builds the x64 NSIS installer from that
same staged tree. It retains the machine installation/shortcuts and legacy
installation directory lookup, uses solid LZMA, and inventories installed files
for upgrades and uninstall. Unrelated files and saved games are preserved.
Compile an existing portable stage with `tools/release/package_nsis.py --stage
<stage> --version <version> --output <setup.exe>`.

Self-contained Linux/MinGW package builds use `lean_images=1` for a private,
checksum-pinned PNG/JPEG/WebP SDL_image. Its cache identity includes compilers,
CMake, codec options, dependency versions and selected library hashes. Linux
installs its canonical decoder under `lib/glob2` with soname symlinks and an
executable-relative search path. Windows staging automatically prioritizes the
private runtime recorded beside its build binary. These builds use a separate
`lean-images` build directory; ordinary builds and RPM retain system libraries.
Snap stages the required JPEG/PNG/WebP runtimes; Flatpak builds the same codec
allowlist directly. PNG/JPEG saving remains enabled for screenshots and maps.

Mac `bundle`/`package` additionally builds a checksum-pinned SDL_image 2.8.12
with PNG/JPEG/WebP loading and PNG/JPEG saving. The cache identity includes
compiler, SDK, codec configuration, dependency versions and the actual libraries
reported by pkg-config. Shared real files are hashed once; unrelated Homebrew
libraries do not invalidate this cache. Missing or changed required libraries
fail packaging rather than falling back to another decoder. The bundle stores
one canonical copy per dylib, preserving required runtime aliases as symlinks.
Its executable is stripped only after a matching dSYM has been retained in the
build's `symbols/` directory, and before dependency rewriting and signing.
Preserve that dSYM with release evidence for crash symbolication.

Q90 changes visual fidelity and packaging bytes, including sprite and team-color
RGB. In-game review should cover animation, recoloring, terrain seams, sheet
boundaries, HD art, cursors and lettering. `--lossless-images` selects exact RGBA
for comparison/rollback; `--original` produces a source-byte comparison tree with per-frame PNGs; it is
not a playable runtime tree for the WebP-only artwork loader.
`--lossy-images` is the optimized default. The former `--lossy-background` and
`--lossless-background` flags are deprecated aliases for these global image flags.
Audits record the global image policy and each selected lossy encoding. No save,
replay, network or simulation format changes are involved. Measure complete
packages and startup separately: smaller compressed assets need not decode
faster or use less GPU memory.

`tools/release/package_sizes.py report --staged-root <root> --archive <package>
--output <report.json>` records payload categories, file hashes, archive sizes,
source revision, architecture and compiler. Its `compare` command rejects
reports from different sources, platforms, compilers or measurement scopes.
Use `--scope asset-only` for an export without a binary/runtime; it must not be
reported as a complete application download. Candidate release CI retains
same-source source-byte/runtime reports for Linux tarballs and Windows ZIPs.
Installed sizes exclude symlink targets counted elsewhere; download size is the
actual archive byte count. Shared system runtimes are outside these artifacts.

GCC/MinGW release experiments use `size_optimization=gc|lto|size`: section
collection, section collection plus LTO, and those options with `-Os`, respectively.
The default `none` keeps current flags. Each experiment has its own
`size-<profile>` build directory and identity, including a separate directory
when combined with `lean_images=1`. They are restricted to native Linux/MinGW
release builds; mobile, browser and Mac builds retain their existing settings.

The candidate release workflow's `benchmark_profiles` input builds all three
experiments. `tools/release/benchmark_profiles.py` compares complete archive
sizes and retains two batches of seven alternating simulation/process-launch
pairs after warmups. It verifies exact per-tick traces, replay orders and saves
from the same two frozen initial states. Adoption needs at least 1 MiB or 1%
archive savings, no credible repeatable simulation slowdown, image/renderer
checks, and a separate actual application-startup measurement within 10%.
`--version` timing is labelled process launch and does not prove GUI startup.
The tool never changes defaults or declares an experiment ready to adopt.
`benchmark_decoder.py` compares stock/private decoders against identical exported
images in separate processes; its scope excludes GPU upload and rendering.

`tools/release/archives.py` compares ZIP levels 6/9 using one stage and keeps the
smaller archive (level 6 on ties). Linux retains gzip and additionally emits xz
only when it saves at least 1 MiB or 5%; xz level 9 must save a further 1 MiB over
level 6. Tar metadata is normalized using `SOURCE_DATE_EPOCH` (zero by default).
These are build-time compression settings; decompressed application content is
unchanged. Compression reports remain review evidence outside package payloads.

## Renderer stress measurements

`torus-render-benchmark` uses the production loaded-map renderer. Its optional
flat-map fixture places real workers,
explorers and warriors in distinct visible cells at the camera's minimum zoom.
Run from the repository root with an isolated profile:

```sh
scons release=1 server=0 torus-render-benchmark
mkdir -p artifacts/render-profile
GLOB2_USER_DATA_DIR="$PWD/artifacts/render-profile/profile" \
GLOB2_BENCH_FLAT=1 GLOB2_BENCH_SIZE=256x256 GLOB2_BENCH_UNITS=1000 \
GLOB2_BENCH_FRAMES=300 GLOB2_BENCH_CAPTURE=artifacts/render-profile/frame.ppm \
build/darwin/client/release/test/torus-render-benchmark -g -F -m -s 1280x800
```

Use the current host's release directory on Linux/Windows. `GLOB2_BENCH_MODE`
selects `2D no clouds` or `2D clouds`; otherwise both run. Set
`GLOB2_BENCH_VISIBLE=1` to show and present the completed fixture.
`GLOB2_BENCH_NATIVE_CLOUD_DETAIL=1` restores the original dense cloud grid for
a controlled comparison. `GLOB2_BENCH_BARS=1` adds health/food bars. Unit count
zero measures the same terrain without units. Retain executable hashes, commands,
GPU identity, logs and captures with before/after comparisons. The flat fixture
checks that rendering leaves the simulation checksum unchanged.

For an AI match, replace `GLOB2_BENCH_SIZE` and `GLOB2_BENCH_UNITS` with
`GLOB2_BENCH_GAME=/absolute/path/to/checkpoint.game.gz`. The saved players and
entities are retained. `GLOB2_BENCH_AI_TICKS=N` advances their AI orders and
simulation for a fixed warmup before measurements. `GLOB2_BENCH_MIN_UNITS=N`
first adds units on free cells until that population is reached; the log separates
the checkpoint's natural population from this deliberately seeded stress case.
`GLOB2_BENCH_FULL_MAP=1` fits the complete map, including on nonsquare viewports,
and can go below the interactive camera's minimum zoom.
`GLOB2_BENCH_CAMERA_SWEEP=1` repeatedly changes zoom and pans across wrap seams.
Sweep measurements mix those view sizes; use a fixed camera for paired timings.
`GLOB2_BENCH_COMPARE_RENDERER=1` additionally compares immediate and optimized
native rendering in the same process, at the same camera and simulation state.
It reports paired process CPU timings and checks pixel differences after timing
ends. Set `GLOB2_BENCH_COMPARE_AI=1` to advance one AI tick before each pair;
combine this with the camera sweep to exercise resource changes and wrap seams.
The comparison uses the no-cloud pass, a fixed water phase, eight warmup pairs,
and a sparse tolerance of at most 100 changed channels with a maximum delta of
1/255. That tolerance does not establish bit-exact moving-scene output. The
immediate reference retains the ordinary resource sprite batch; it disables the
mixed unit queue, texture arrays and persistent map geometry.
`GLOB2_BENCH_COMPARE_CAPTURE_PREFIX=artifacts/render-profile/pair` saves the final
pair as `pair-immediate.ppm` and `pair-optimized.ppm` for visual review.

Timings include GPU completion (`glFinish`) and exclude frame presentation, AI,
input and simulation work. They are renderer measurements, not whole-game FPS.
POSIX builds also report process CPU time separately from elapsed time.
Scope timings separately report CPU submission and overlap; do not sum inclusive
scopes. The fixture is native OpenGL only; mobile uses the SDL portable renderer,
so desktop results do not qualify Android/iOS hardware performance.

The native client exports production skin artwork without menu, audio or
simulation startup:

```sh
glob2 --skin-render-info
glob2 --render-skin --manifest skin.json --texture texture.png \
  --material material.png --output-dir sprites
```

The server authorizes entitlement before queuing this local command. The source
manifest contains `skinId`, `layout: colony-v2`, `buildingColor`, the two source
SHA-256 values, `manifestSha256`, and optional swarm mesh and integer angle. The
CLI validates the canonical manifest identity, bounded 512×512 source images,
opaque paint and discrete opaque material ids. It requires a native OpenGL
context and the pinned libwebp version in `tools/image_encoding.json`; Linux
workers use `SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2`
under `xvfb-run -a`. Run from the installed data directory.

Seven clips each contain eight directions and 32 phases, packed into four
1024×1024 pages of 64 transparent 128×128 tiles. The selected rotated swarm has
one 128×128 image. Production framebuffer readback preserves top-to-bottom image
orientation and 1.25 padding, omits the separate ground shadow, and converts
premultiplied pixels to straight alpha for storage. Each page compares lossy WebP
Q90/method 6 against lossless WebP Q75/method 4 and keeps the smaller candidate
(lossy wins ties); both use exact alpha. This shares the versioned encoding
recipe with bundled assets and considers only WebP candidates for skin sheets.
The encoder verifies unchanged alpha for both encodings and exact RGBA for
lossless output. The JSON bundle records source identity, render revision,
logical sizes, padding, frame mapping, and page hashes and byte counts.
`manifest.json` is written last; the complete staging directory is renamed
atomically. Existing output directories are never overwritten.

The render revision hashes the meshes, production shaders, view transforms,
animation mapping, layout and encoding recipe. Changing these inputs regenerates
derivatives while existing matches retain their pinned bundle.
 Focused validation
uses the `SkinAuthorization`, `SkinDownloads`, `SkinSprites` and `SurfaceCoverage`
unit suites plus the skin-render worker and API tests. To exercise the actual
worker adapter, run its opt-in `native.test.ts` under Xvfb with
`GLOB2_SKIN_RENDER_TEST_BINARY` set to the absolute built client path.

The `skin-game-preview` diagnostic measures the colony-skin path through the real
Scene renderer. Build it with `scons release=1 skin-game-preview`, then set
`SKIN_PREVIEW_SAVE` to a two-colony saved game, `GLOB2_SKIN_PREVIEW_DIR` to a
mesh directory containing the colony-v2 `paint.webp` (a 512x512 colour atlas with
one 256x256 quadrant per model: worker, warrior, explorer, swarm) and optionally
`material.webp` (the matching 512x512 material-id map; absent means all glossy),
`SKIN_PREVIEW_CAPTURE` to a capture name, and `SKIN_PREVIEW_BENCHMARK` to a
relative capture prefix. Set `GLOB2_SKIN_PREVIEW_SWARM` to a swarm mesh id (such
as `crown`) to draw team 0's swarm with that mesh; the directory then needs its
`swarm-<id>.gsk`, and the swarm quadrant paints it. Run with `-g -m
-s800x600` and an isolated `GLOB2_USER_DATA_DIR`. `SKIN_BENCH_FRAMES` and
`SKIN_BENCH_WARMUP` control total and discarded warmup frames (defaults 45 and 5).
For signed software artwork, replace the preview directory with
`SKIN_PREVIEW_ASSIGNMENT` (a JSON file containing `origin`, `matchId` and signed
`colonySkins` tickets) and `SKIN_PREVIEW_CACHE` (an isolated cache directory).
Use `-G` for software or `-g` for OpenGL with the same assignment and save.
`SKIN_BENCH_TEAMS` controls the number of colonies in the crowd; their tickets
can share or select different skins. `SKIN_BENCH_FRAME_PREFIX` captures 32
animation frames for comparison videos. Software measurements also report
decoded sprite memory.
Decoded pages compact transparent pose margins with a one-pixel filtering guard,
preserving their original resolution and placement while accounting their packed
allocation against the shared cache limit.
The harness adds a crowded diagnostic colony, advances its animation phases,
and checks that every draw preserves simulation checksums and that classic and
skinned states match. It reports first-frame cost separately from warmed mean,
p95, draw counts and `render.skins.*` preparation/geometry/raster/composite scopes.
Set `SKIN_PREVIEW_ZOOM` (0.02–5.0, clamped by the map camera) to exercise adaptive
zoom detail. Set `SKIN_PREVIEW_ADAPTIVE=0` to check skins with adaptive detail
disabled. When only overview markers and building icons are visible, the
diagnostic checks that hidden skin meshes are neither prepared nor drawn.
Frame times include presentation; scope times measure CPU submission and driver
work, not isolated GPU duration. Preserve the fixture, binaries, build inputs,
resolution, driver, counters and captures for matched comparisons; run repeated
alternating pairs without concurrent builds. Software GL results do not establish
hardware performance. The smaller `skin-preview ASSET_DIRECTORY OUTPUT_PREFIX`
renders every clip from the same 512x512 `paint.webp` and optional `material.webp`
(create both with `python3 tools/skins/make_paint.py DIR --material mixed`), each
clip sampling its own model quadrant. Its `--validate-opacity` diagnostic captures opaque, half-opacity and invisible mesh/shadow
composites and verifies that opacity changes reuse cached poses. The
`--validate-cache` diagnostic checks cache hits, repainting, material-map and
region changes, texture address reuse and atlas overflow, and saves images for pixel comparison.

Cloud patches in the flat game and editor views now use a coarser, world-anchored
lattice when zooming out to half size or smaller. Patches retain at most their configured
1:1 size on screen; normal zoom keeps the original sampling. The field and animation
time remain unchanged, but distant clouds have less fine detail. The torus view
retains its separate sampling budget.

Point bars batch opaque fills within each bar using bounded OpenGL or SDL geometry
submissions. OpenGL outlines and translucent fills preserve their original order;
software surfaces retain their existing path. Full-map terrain and resource passes
skip fog discovery queries when `DRAW_WHOLE_MAP` already makes every tile visible.

Flat-map resources use a bounded OpenGL/portable SDL sprite batch, including
standalone frames from partial HD packs. Draws sharing a texture and alpha can join an earlier run
only when their rectangles do not overlap intervening runs. Conservative bounds
preserve the order of overlapping artwork while reducing draw submissions and
texture switches without changing sampling or allocating another texture atlas.
OpenGL texture uploads flush pending draws, and the scope flushes before leaving
the resource pass. Software surfaces and dynamic team-color sprites retain their
existing paths; cache-backed team-color surfaces cannot be deferred safely.


For comparisons with another revision, set `GLOB2_BENCH_PAUSE_PRESENTATION=1`
to freeze the water phase and `GLOB2_BENCH_WARMUP_FRAMES` to the same number of
frames on both executables. Record cold-frame samples as well as steady-state
medians, and confirm `STEADY_CACHE pending=0` before describing results as fully
warmed. Compare complete builds from both revisions; the diagnostic immediate
path is not an untouched-master baseline.

### Batching and geometry cache invariants

The native desktop OpenGL renderer batches ground and air passes with
`UnitDrawBatch`. It keeps the original sprite, fill and line primitives and their
painter order. A draw may join an earlier run only if its conservative bounds do
not intersect any intervening run. Bounds include a physical-pixel sampling
margin and half the requested outline width; they must be expressed in the
current map transform. Capacity limits flush a batch rather than grow it without
bound. Unsupported commands submit pending draws first. Clip and transform
changes also flush, then disable culling and reordering for the remainder of the
scope because the original bounds no longer describe its coordinates.

Team hue and opacity travel with each vertex. Compatible HD textures can share
array pages while retaining their original dimensions, mip levels, format and
sampling parameters. The source textures remain available for fallback drawing.
The array cache caps its additional texture payload at 64 MiB, separately from
cached map geometry. Immutable slots survive source texture invalidation until
context teardown; new textures use the ordinary path when that budget is full.
Pages contain at most 64 layers to bound each driver allocation. `FrameDrawBatch`
defers new array copies until after scene submission, attempting at most eight
sources under a soft 2 ms budget. Original textures draw while preparation is
pending, and mutation/deletion cancels pending IDs. A texture
upload or deletion must flush commands referring to the old pixels and invalidate
array views before the driver can reuse a texture name. Cached geometry must also
be invalidated selectively when its source texture or array page changes; unrelated
uploads must preserve reusable entries. A mutation serial rejects interrupted
captures but does not globally clear the cache. These are
presentation caches owned by the graphics context, released while that context
is current; they are neither saved nor consulted by simulation code.

Terrain uses the shared CPU material compositor and bounded 16 by 16 cell pages
on software and GPU backends; see [terrain materials](../assets/terrain-materials.md).
Fully revealed resources use canonical map rows, with sorted source-tile indices
selecting the contiguous visible vertex range. Translation places a canonical row
at its wrapped-map position, so camera panning does not change its vertices.
Each resource entry compares its exact frame/visibility vector before reuse.
Partial-discovery resources keep the ordinary drawing path. The resource geometry budget
is 32 MiB of buffer payload with at most 4096 entries and least-recently-used
eviction; CPU metadata and driver allocation overhead are additional. Each scene
attempts at most 16 geometry builds under a separate soft 2 ms budget; validated
cache hits remain unrestricted. Deferred rows retain ordinary sprite batching.
These time limits are soft because an individual driver call can exceed them.

Native sprite atlases keep a one-texel extruded border around every frame,
including equal-size terrain tiles. Fractional zoom and camera offsets can put
a covered pixel arbitrarily close to a frame edge; the nearest-sampling tie
bias must land in that frame's border rather than a neighboring frame. Keep
the border copy unblended so transparent edges retain their original RGBA.

Native array/cache optimizations require supported desktop OpenGL features.
Software, portable SDL and unsupported native contexts retain their existing
rendering paths. Desktop measurements must not be presented as phone performance.
When changing this code, compare immediate and batched output at several zooms,
across wrap seams and clip boundaries, with overlapping translucent sprites,
wide outlines, carried icons, texture mutation/deletion and context recreation.
Advance an AI match between comparisons to exercise resource invalidation, and
check that each render leaves its simulation checksum unchanged. Keep commands,
seeds, binaries, captures and timing data under `artifacts/` for review.

### Skin materials

Colony-skin materials are declared once in `libgag/shaders/skin-materials.json`
(ids, keys, display names, picker groups, which materials grow fur shells, the
shell count and the fur length and depth bias every renderer uses) and shaded
once in `libgag/shaders/skin-material.glsl`. `scons/skin_materials.py`
compiles both into the generated `include/glob2/SkinMaterials.h`
(`SKIN_MATERIAL_COUNT`, `SKIN_MATERIAL_SHELLS`, the `SkinMaterials` table and the
GLSL text) for the desktop, web and mobile builds; Colony Studio imports the GLSL
raw, and the protocol package carries a mirrored `COLONY_SKIN_MATERIALS` list that
`packages/protocol/test/skinMaterials.test.ts` pins to the JSON.

Every material fills a `SkinSurface` (albedo, perturbed normal, roughness,
specular, metal, wrap, rim, cel, emissive, alpha) and one `skinLight` lights them
all, so the catalogue stays consistent. Meshes carry no tangents: perturb normals
with `skinTilt` from a UV-space height gradient (`SKIN_GRADIENT`), never from
tangent-space maps; scale micro-frequency octaves by `s.detail`, which fades to
0 as texels shrink below pixels, so grain shows in the studio but never aliases
in the baker's 128 px tiles. The wrappers only declare varyings and call
`skinShadeAtlas` (mesh renderers, with `SKIN_TEXTURE` defined per dialect) or
`skinShadeSphere` (swatches). Materials with `shells: true` are drawn
`SKIN_MATERIAL_SHELLS` extra times with vertices pushed along the camera-space
normal; their shader sets `alpha` to 0 where a shell carries no strand. Tiles
are cached per pose, so no material can animate over time.

To add a material: append it to the JSON, add `skinMaterial_<key>` and its
dispatch line to the GLSL, mirror the entry in `platform/packages/protocol/src/skins.ts`,
then run `test/build_system/test_skin_materials.py`, the `SkinMesh` display
suite with `GLOB2_UPDATE_SKIN_FINGERPRINTS=1` once (it rewrites
`test/fixtures/skins/material-fingerprints.json` and writes contact sheets under
`artifacts/skins/materials/`) and review the sheets. While iterating on the
GLSL, `tools/skins/material_spheres.mjs` (run from `platform/apps/web`) renders
every material on a sphere through headless Chromium in seconds, and the
`skins-materials.spec.ts` e2e captures each material on the worker at studio
resolution. Any shader edit changes the
sprite render revision and re-bakes every published skin.

## Simulation verification and diagnostics

A `Team` is a colony; a `Player` controls a team, and several players can share one.
For timing and scheduling, start with `src/game/Game_sync.cpp` and `src/engine/EngineRun.cpp`.

- Use `Utilities::syncRand()` for simulation randomness. Keep iteration and tie
  breaking deterministic; never depend on pointer ordering, hash-table iteration,
  thread scheduling or wall-clock budgets for simulation decisions.
- Each AI controller has a saved random stream derived from the game seed and player
  number. AI implementations receive that stream when created or loaded. During
  `AI::getOrder()`, legacy helper calls to `syncRand()` are routed to the same AI
  stream. This keeps one AI's random draws independent of other controllers' poll
  order, but does not make their shared map and caches safe for concurrent access.
  A controller must still have at most one `getOrder()` in flight; its stream
  and decision state are mutable.
- Each `Game` owns its synchronized stream (`Game::syncRandom`), saved and restored
  with the game. `Game::syncStep`, `Game::executeOrder`, load and save bind it with
  `SyncRandScope`, so the simulation draws from the game it advances on whichever
  thread runs it. Other code that advances a game's simulation must bind it with
  `Game::bindRandom()`. Outside a bound scope, `syncRand()` uses a `thread_local`
  default stream that map generation and other tools seed for themselves; a new
  thread starts from the default seed. During an engine session an unbound draw is
  a determinism bug: it is counted (`unboundSyncRandDraws()`), and
  `GLOB2_SYNC_RAND_STRICT=1` aborts on it.
- Keep rendering, particles, animation and other presentation-only randomness off
  `syncRand()`. Use a presentation-owned generator such as `GameGUI::effectsRandom`,
  so visual effects can change, run at any frame rate or move to another thread
  without consuming simulation draws.
- Simulation/client boundary (`src/engine/sim/`). Simulation code must not call `GameGUI`;
  it talks to the client through three channels, which `GameGUI` owns and `Game`
  points to (all null without a GUI):
  - `ClientEvents`: lossless queue of notices the simulation publishes (team
    `GameEvent`s, chat, voice, marks, pause, ghost removal, building removal, unit
    conversion, executed orders) plus a per-tick latest-value pulse
    (`Team::wasRecentEvent` for every team). `Game::executeOrderAndNotify` publishes
    the order effects; `GameGUI::consumeClientEvents` applies them after each order,
    after each engine tick, and at the start of `step` and `drawAll`.
  - `ClientCommandSink`: the presentation commands map scripts issue (building and
    flag choices, GUI elements, highlights, Space swallowing, script text). SGSL,
    USL and JavaScript map scripts call it instead of `GameGUI`. Its two read
    methods are legacy USL queries; do not add more.
  - `ClientRequests`: a latest-value `ClientView` (viewport, observed building,
    overlay, debug layers) and a lossless command queue (the SGSL Space
    acknowledgement). `Game::applyClientRequests` applies them at the start of
    `Game::syncStep`; only the observed building records
    `Building::unitsFailingByReason`.

  Client code holds entities as `BuildingRef`/`UnitRef` (gid plus `scriptIdentity`)
  and resolves them through `Game::resolveBuilding`/`resolveUnit` at each use; do not
  keep `Building*`/`Unit*` across ticks in client code. The channels are
  single-threaded for now. Making them thread-safe only changes `LosslessQueue` and
  the latest-value accessors.
- For behavior-preserving refactors and optimizations, compare base and changed
  builds using identical saves/maps, seeds, settings and orders. Compare per-tick
  state/checksums as well as replay bytes: matching orders alone do not prove that
  clients computed identical states.
- For changes affecting simulation portability, run the same retained scenarios
  across the affected compilers/platforms and compare traces. Separate repeatability
  on one machine from cross-platform equivalence. CI build success alone proves neither.
- Check save/load continuation when state or scheduling changes. A derived cache
  can still affect future decisions; do not assume that it is safe to discard.
  Treat save-format, replay and network compatibility as separate questions. If
  simulation rules change, assess replay acceptance and protocol/version gates even
  when the saved byte layout is unchanged.
- Team capacity is `Team::MAX_COUNT` (16), shared by colonies and controller slots.
  `MAX_COUNT_ON_DISK` (32) is the fixed GameHeader player/alliance layout, not a
  selectable match size. Team masks are 32-bit; packed growth coverage requires
  three masks to fit a 64-bit word (at most 21 teams in that representation).
  Unit/building identifiers must also fit below the 16-bit empty-entity sentinel.
  Team iterators must use the live match count; a full array has no null end slot.
  Format 127 counts Maxima opponent records and script-generation team slots;
  older formats retain their historical 12-slot layouts. Text header alliances use
  indexed slots from format 127; binary header bytes stay unchanged. Custom-game
  preferences version 4 counts colony records and still reads the twelve records
  written by versions 1–3. The building-generation
  plane must be remapped when loading old saves. Never substitute a live capacity
  for a historical serialized length. Save floor 58 remains unchanged.
  Warrush probes one capacity slot every two ticks (32 ticks for sixteen slots).
  Empty slots fall through to normal decisions, preserving smaller-match timing.
  Replay floor 127 gates the new capacity; network protocol 51 additionally
  requires the format-128 compact save reader. Older saves load into the current
  simulation.
- Versioning rule: when the save format changes, bump `VERSION_MINOR` and preserve
  older saves through version-gated loading, or explicitly document an approved
  compatibility break. When simulation changes invalidate old replays or mixed-client
  games, update replay acceptance, `NET_PROTOCOL_VERSION` and `SIM_REVISION` as needed.
  Test acceptance/rejection at the version boundaries; unchanged saved bytes do not
  establish replay or network compatibility.
- `.map`/`.game` files are gzip level 6 by default (`FileManager::writeGzipAtomic`/
  `writeGzipAtomically` in `libgag/src/FileManagerGzip.cpp`; the gzip header carries
  a zero timestamp and OS=unknown, producing deterministic bytes with the same
  zlib encoder). This is a container change, not a save-format schema change: the bytes
  inside the gzip stream are exactly what today's serializer already writes, so it
  does not need a `VERSION_MINOR` bump. Reading is transparent and extension-driven:
  `FileManager::openInflatingInputStreamBackend`/`glob2OpenMapOrSaveInputStreamBackend`
  inflate a `.gz`-suffixed path and reject corrupt/truncated gzip data; a raw legacy
  file with no `.gz` sibling still loads unchanged. `glob2PreferGzipReadPath`/
  `glob2GzipWritePath`/`glob2ListMapOrSaveFiles` (`src/map/io/MapHeader.cpp`) are
  the read/write path-resolution helpers most call sites should use rather than
  hand-rolling the `.gz` suffix logic. Replays and network protocol gates are
  unaffected.
- Intentional bug fixes or gameplay changes may change old outcomes. Explain the
  difference and test the intended behavior rather than claiming old/new equivalence.
- Terrain simulation properties retain the fixed layout in `src/map/TerrainProperties.h`,
  indexed by stable 16-bit `TerrainType` IDs in a map-owned immutable `TerrainRegistry`.
  Use `map.terrainProperties(type)` or `map.terrainPropertiesAt(...)`; the global
  constexpr table defines only the seven built-ins. Walking, swimming, flying, building eligibility,
  resource habitats, irrigation, movement rates, health and projectile obstruction
  are independent capabilities. Use a property predicate when asking what a cell
  permits; compare IDs only when its identity is the actual question (for example,
  an editor brush or a generator's material selection).
- `Map::terrainSeed()` is presentation state saved with the map (format 138): it salts
  the terrain material hashes so maps look distinct; generators derive it from their
  request seed and the editor can reroll it. It is never read by simulation code and
  is not in `checkSum()`; see [terrain materials](../assets/terrain-materials.md#map-seed).
- `Map::terrainTypeAt` reads the canonical ID plane. `Tile::terrain` is presentation
  state: its sprite frame must never determine gameplay. Use `setCellTerrain` and
  batch edits with `editTerrain()` so snapshots, topology and ecology caches stay
  consistent with the canonical IDs. The compatibility `getTerrainType` query returns an
  unknown category for legacy shores; never use it to index the property table.
  The old corner editor and old-file importer are explicit
  adapters; legacy shores have their own walkable, unbuildable profiles.
- Saved sprite ranges, corner semantics and authoring frame selection are frozen in
  `TerrainCompatibility.h`. Detailed terrain rendering resolves shipped appearances
  through a presentation-only material catalog, corner coverage resolver and CPU
  compositor. `data/terrain/tileset.json` defines those materials independently of
  gameplay IDs; `TerrainPresentation.h` retains semantic editor and image-interchange
  metadata. See [terrain material authoring](../assets/terrain-materials.md) for
  variants, boundary profiles, asset validation and cache behavior. Visual catalog
  changes must not change saved frames or simulation RNG use.
- Built-in terrain is table-driven. `TerrainGroup.h` defines one property profile per
  gameplay group; `TerrainTypeTable.h` lists every `TerrainType` with its group, external
  name, string-table label, semantic colours and frozen saved-frame range, and the
  `TerrainProperties.h`, `TerrainPresentation.h`, `TerrainCompatibility.h` and
  `TerrainExperiments.h` tables derive from it. Members of a group are byte-identical
  profiles, so the registry deduplicates them into one property index; use
  `terrainGroup(type)` for palette and reporting buckets, never for simulation rules.
  Adding a type is one enumerator, one row, one label and one material binding;
  adding a group is one profile and, when gated, one `ExperimentId`.
- Format 140 raised `TERRAIN_COUNT` from 7 to 31. Custom definitions and tile IDs in
  older files start at 7, so `Map::loadTask` remaps IDs at or above the file's built-in
  count (`TERRAIN_COUNT_BEFORE_CATALOGUE`) to follow the current built-ins, and
  `TerrainRegistry::deserialize` takes that count. Built-in-only files are unchanged
  byte for byte; custom registries re-serialize with shifted IDs, so their digest
  changes and replays from formats 136 to 139 that embed one no longer verify.
- Runtime types inherit a shipped appearance and use full tiles; legacy corner
  adapters apply only to built-ins. Any paintable built-in is a valid `base` or
  `appearance`. Import definitions through
  `Map::importTerrainDefinitions` before a match or in the editor. It validates and
  compiles the complete replacement before publishing it, preserves existing IDs,
  and appends new keys in sorted order. Scenes and gradient jobs retain the same
  registry snapshot; inner loops borrow indexed data. Scenes cache the shipped
  appearance in a two-byte cell plane; the compositor resolves equivalent aliases
  to the same material without scanning custom definitions. Render caches bind the
  registry snapshot and actual asset revisions. Saved custom colors remain
  authoritative for previews and minimaps; built-ins use catalog palettes.
  Experimental authoring gates live in `TerrainExperiments.h`; maps carry required
  experiments into matches, while saves retain them independently of user settings.
  A runtime definition whose properties equal a gated built-in group's profile
  requires that group's experiment too (`Map::requiredTerrainExperiments` compares
  property indices); a definition with its own profile stays ungated.
- Trail retains stable terrain ID `4` (`TRAIL`) and experiment position `3`
  (`TrailTerrain`). Its external name, translation keys and serialized experiment
  key remain `road` / `road-terrain` for scripting, reports, editor actions and
  existing files. Classic frames 288–303 come from `datasrc/gfx/trail/`; the
  material catalog independently chooses the detailed appearance for that ID.
- Ecology caches terrain-only land and aquatic fields for the map's lifetime.
  Normal growth, harvesting, unit movement and building placement do not rebuild
  them. Map replacement invalidates them; terrain edits invalidate them only when
  effective fertility contributions, inhibition, shore support or local growth
  factors change. Habitat-only edits update one cell's resource mask, and other
  capability changes retain the fields. A query inside an edit batch observes all
  preceding changes; closing the batch does not discard an already-current field.
  The weighted kernels preserve the classic paired water/inhibition and rotated
  shoreline probes; growth reads their cached results. Fields use Q16 integers,
  while opportunity rates use `Fertility::kRateScale` (three times Q16) so wheat
  retains positive growth even at the smallest nonzero fertility. Keep these
  units distinct. Weighted contributions below one Q16 quantum round down;
  classic terrain probabilities retain their exact integer numerators.
  `Tile::canResourcesGrow` is the saved scenario override;
  `Map::canResourcesGrow` also checks the terrain capability.
- Save format 136 embeds custom IDs, keys and fully resolved properties and presentation
  before the tile data. Legacy numeric presentation fields are retained verbatim
  for round trips and checksums; the material catalog controls detailed drawing.
  Bounded JSON byte chunks support binary and text streams.
  Serialization emits definitions in canonical ID order, with object fields in key
  order, and import/load
  releases the parsed JSON tree before compilation to bound temporary memory.
  Loading rebuilds compiled tables before restoring dependent caches;
  it never consults authoring JSON files. Earlier files use the built-in registry;
  pre-134 files also derive canonical IDs from legacy sprite ranges. Save floor 58
  remains unchanged. Building format 137 adds the per-game building catalog; replay
  floor 137 and network protocol 57 introduced those simulation/catalog gates.
  The current replay floor is 139 for the completed-tick observation phase.
  Custom registry checksums hash canonical serialized fields, not struct padding.
  Built-in-only maps keep their previous terrain checksum contribution. Existing
  map-content hashes cover the embedded section for LAN, online and verification.
- Registry compilation calculates movement and air costs once, deduplicates cost
  profiles and caches distinct edge steps. Runtime gradient setup scales with
  distinct profiles, not registered IDs. Uniform, binary swimming and general-cost
  kernels dispatch outside cell loops. The general kernel has scalar, SSE2 and NEON
  implementations and compiled 64/128/256 bucket rings. Map counts select the smallest
  safe ring from terrain present; unused slow definitions cannot enlarge it. Search
  setup validates reachable edge costs against the selected ring before changing a
  field, because a too-small ring can alias a future cost layer. Keep validation out
  of cell/neighbor expansion; compact production snapshots bound it by distinct costs.
  Capability counters keep health, air and projectile shortcuts independent of
  registry size. A* retains the historical built-in lower bound and lowers it only
  for faster custom terrain actually present, preserving old route choices.
  Map property queries use a derived two-byte index plane into deduplicated
  fixed-layout property structs, keeping equivalent custom IDs out of the hot
  property working set. Canonical tile IDs and persistence remain unchanged.
  Maps lazily cache a one-byte cost-profile plane and only the distinct costs
  present in that plane per queried swimming class,
  removing the ID-to-profile lookup from general-cost cell loops. These planes
  share ownership with searches/jobs and invalidate together with terrain snapshots.
  Eager fields, resumed building searches, worker snapshots and strategic travel
  share compiled integer costs and reusable scratch storage.
- Runtime-terrain performance qualification compares equivalent maps with 7, 259
  and 1,024 definitions, plus distinct-cost and 16,384-type stress cases. Use release
  builds on a quiet machine, warm up, randomize paired execution order and collect
  at least ten repetitions. Report CPU and wall time separately, with rendering
  and memory costs. Repeatable regressions over 2% full-match CPU or 5% terrain
  kernel time block acceptance; noisy measurements do not establish a pass.
- Keep the terrain index domains explicit when changing this code:
  canonical `TerrainType` IDs identify saved definitions; property indices select
  deduplicated simulation structs; per-swimming-class profile bytes select movement
  costs; scene appearance IDs select shipped visual materials. None is a valid
  substitute for a canonical ID in serialization or scripts. These derived planes
  are rebuilt from the registry and cells, never serialized. Registry factories
  publish `shared_ptr<const TerrainRegistry>`; copying a registry is private because
  authoring presentation strings borrow its owned key/name storage.
- The engine has a completed-tick observation phase. `Game::syncStep` first runs
  all world mutations, including fog, projects and scripts, then selects/reserves
  one periodic gradient job. Engine defers private seeding into its next
  `ReadOnlyPhase` alongside AI decisions. This is the default architecture; the
  compute mask and thread count select execution only, never observation timing.
  Direct `Game::syncStep` callers complete preparation before returning unless
  they explicitly request `PreparationCompletion::Deferred` and own its barrier.
  Standalone `Map::syncStep` retains synchronous map-level preparation.
- `ReadOnlyPhase` borrows groups of callbacks and runs them through one
  `ComputeExecutor` barrier. World state must stay stable until every task leaves,
  including on exceptions. Tasks may change their own controller/private results
  and synchronized derived caches. Bind shared AI telemetry before dispatch;
  publish orders afterwards in player order. Add further work only after auditing
  scratch ownership, RNG use, input lifetime and every shared cache it touches.
  Script observation and on-demand building gradients retain their existing
  scheduling and are not automatically independent observation tasks.
- Gradient selection, round-robin flags and queue membership stay on the simulation
  owner. A typed reservation is visible to AI lazy invalidation before dispatch;
  preparation writes only its private seeds and immutable terrain snapshots.
  Propagation may then outlive the observation barrier, but publication remains
  after its configured delay (eight ticks by default), before team stepping.
  Worker count and completion time never
  select publication time. Saves, compute/terrain reconfiguration and subsequent
  mutations drain preparation; teardown discards its descriptor before resetting
  the queue. Seed/dispatch failures mark the job completed with an error, preventing
  a save or publication from waiting indefinitely. Inspect these contracts before
  adding parallel work; sharing the executor alone does not establish safety.
- Gradient field seeding lives in the area, building and resource source files.
  `MapGradientPropagation.cpp` starts eager fields through the private
  `src/field/GradientPropagation.h` core; `BuildingGradientSearch.cpp` resumes
  building fields. Both use `src/field/GradientRelaxation.h`. Keep their cell-cost
  and queue ordering contracts shared when tuning architecture-specific kernels.
  `src/field/GradientConstants.h` owns the field encoding; `Map` keeps its pipeline and
  per-executor scratch in an opaque `GradientRuntime`. Save/load reaches pending
  work through snapshot views, not the pipeline's mutable jobs.
- `src/field/` is the Map-independent field library. Weighted paths retain
  bucket queues and scalar/SSE2/NEON relaxation; uniform four/eight-neighbour
  fields use an ordered FIFO with caller-owned payloads and admission rules.
  Seed and neighbour order matter for first-discovery payloads and early stopping,
  including Cortex wheat depth and Maxima food claims. Keep those searches ordered.
  Callers own seeding, field encodings, transient scratch, cache ages and publication.
  Sharing a solver does not make fields with different predicates interchangeable.
- Choose the smallest field operation that preserves the caller's contract:

  | Operation | Entry point | Caller responsibility |
  | --- | --- | --- |
  | Weighted path field | `gradient_kernel::propagateField` | Encode seeds/obstacles, supply stable terrain and a `GradientWorkspace`. |
  | Resumable weighted paths | `gradient_kernel::expandBucket` | Preserve pending buckets and settle whole cost layers before pausing. |
  | Uniform distance field | `field::expandDistances` | Seed equal distances, choose the unvisited sentinel and ordered stencil. |
  | Ordered FIFO with payloads | `field::traverse` | Admit and enqueue neighbours; retain first-discovery payloads and stopping rules. |
  | Domain heap search | `field::traversePriority` | Own costs, comparator, stale-entry checks and parent ties, including zero-cost edges. |
  | Component stack/queue | `field::depthFirst` / `field::breadthFirst` | Own discovery and push order. |

  `Grid::neighbors` supplies raw coordinates for bounds checks before wrapping;
  `Grid::neighborIndices` supplies wrapped indices. Both retain stencil order and
  aliases on thin grids. The vector FIFO keeps discovery history; `Frontier`
  consumes entries and retains storage for the largest pending frontier. Clear
  and seed either workspace at the owning caller. An early stop preserves writes
  already made; grid traversal finishes the current neighbour stencil before
  visiting the next entry. Use `breadthFirst` for stops during expansion.
- Influence has two distinct contracts in `src/field/Influence.h`: convergent
  maximum-contribution propagation and four directional sweeps. Castor requires
  the latter's staggered scan order and byte arithmetic; replacing it with
  convergence changes AI decisions. Map retains the cooperative checkpoints
  around convergent rows. No solver depends on Map, AI, threading or serialization.
- In `src/map/gradient/MapGradientChamfer.cpp` the chamfer distance transform's
  convergence-pass cap is bounded by the Uint8 value range (256), not by the
  Borgefors 1-pass result. Borgefors holds only on an obstacle-free grid; with
  obstacles each bend in the propagation path costs about K/2 passes, and real
  128×128 maps needed well over 8. The cap is a tripwire for monotonicity
  violations, not a throttle. Do not derive a tighter bound from grid geometry.
- A candidate comparison used only to pick the best of several options (which unit
  to hire, which move to take) must not allocate, rebuild or refresh anything it
  touches, including cache-use timestamps. If scoring can trigger the same side
  effects as actually doing the work, "read-only" claims about it are false and any
  performance comparison built on it is unreliable.
- Never bound simulation work by wall-clock time or a timeout: this is a lockstep
  engine, and two machines running the same tick at different real speeds must still
  do identical work. Use a fixed count of ticks, steps or comparisons instead.
- A new regression test only protects the codebase once
  `.github/workflows/build.yml` actually builds and runs it; one that only runs by
  hand, once, is not a regression test. Add its translation unit to `test/tests.py`:
  the unit or engine binary is already in the job's single "Build glob2 and the
  regression harnesses" command and `test/run_tests.py` picks the new cases up on
  the next run, so no per-test step is needed. A harness that must stay a
  separate program (two processes, a golden-table tool) gets a `PROGRAMS` entry
  and one step that only runs it: a separate `scons` call per step re-reads the
  whole build and compiles one file at a time. Builds that need other options
  (`role=relay`, `opengl=0`) belong in
  the `linux variants` job, and long CPU-bound checks in a job of their own, as the
  golden-map sweep does; its four sweep shards are split between two jobs per
  toolchain, alongside a job for telemetry and generator defaults. These jobs reuse
  the main Linux build artifacts when native checks are selected; map-only diffs
  build the required programs themselves. Tests run in parallel after the shared
  build. The Linux variants matrix owns the relay build; the main Linux jobs do
  not repeat it.
  Browser checks follow the same rule: build once per job, pass outputs to the
  test jobs as artifacts, and shard long suites rather than lengthening one job.
- A map generator's `revision` is enforced by `MapGeneratorGoldenTest`: a seed's map changing
  while the revision stays fails the check, so bump the revision and run `--update` together
  (see the framework reference). `--sweep` there is the first thing to run after touching
  colony placement; a cell it fails is a "Generation failed" a player would see.
- When fixing a bug, confirm the regression actually fails against the unpatched
  code before trusting that it passes against the fix. A test that passes either way
  is not testing the bug.
- A cache or other retained state with no eviction policy needs an explicit bound —
  a count or a byte budget. "It would take an enormous game to reach" is not a bound.
- For suspected uninitialized reads on macOS, `DET_INIT=zero` versus `pattern` and
  allocator scribbling (`MallocPreScribble=1 MallocScribble=1`) can help isolate
  the cause where Valgrind/MSan are unavailable. Repeat the same seed serially in
  each build: stable runs that differ between initialization modes suggest an
  uninitialized read. Instability within a mode needs further investigation.
  SCons does not track `DET_INIT`:
  rebuild affected objects when changing it. Report the actual sanitizer or diagnostic
  coverage and its limits.


### Terrain gradient benchmarks

Engine movement profiles are prepared once from the compiled terrain table in
`src/field/PreparedTerrainCosts.h`. Terrain identities with the same cardinal and
diagonal entry costs share a cost class; equal edge costs share queue destinations,
including cardinal/diagonal aliases. Eager propagation can select a one-class
kernel only after checking every non-forbidden cell, including source cells.
Lazy searches retain the profile selected by their captured swimming class and
an immutable terrain snapshot. Each search or worker owns its mutable queue;
prepared profiles contain no search state and introduce no serialized cache.

Strategic AI travel fields in `src/field/TerrainTravel.h` use a separate bounded
integer queue. Their historical metric charges all eight neighbors the same
terrain entry cost, then rounds the completed wide distances to tile units. Do
not substitute the engine's cardinal/diagonal metric or round intermediate costs.

`tools/gradient_benchmark.py` builds an opt-in standalone, paired benchmark; it
needs a C++20 compiler but no SDL or game build. Capture the pre-optimization
source when comparing against the original terrain kernel:

```sh
mkdir -p artifacts/gradient-baseline
# This historical revision is the reference accepted for this optimization.
git archive 3266c8e51 src/field src/map/TerrainProperties.h src/map/TerrainType.h | \
  tar -x -C artifacts/gradient-baseline
python3 tools/gradient_benchmark.py \
  --baseline-dir artifacts/gradient-baseline/src \
  --output artifacts/gradient-bench --suite representative --repeats 11
```

The runner copies candidate headers and harness source before compiling, records
compiler/flags and SHA-256 hashes, and writes raw JSONL samples plus per-case
median comparisons. `--cpu N` pins the subprocess on Linux. `--scalar` forces the
scalar implementation; otherwise the compiler target selects SSE2 or NEON.
`--suite full` adds 64² and 256² cases; `--suite smoke` reduces the main timing
matrix to 32² while retaining the correctness corner cases. The baseline adapter
is specific to the historical revision above and rejects changed source anchors
rather than silently omitting counter hooks. Use a fresh output directory for each
comparison to retain its raw evidence.

`--case '{"size":128,"pattern":"network","swim":3,"mode":"terrain"}'` selects
one custom case; repeat the option for a custom matrix. Optional keys are `width`,
`height`, `registry`, `costs`, `seeds`, `travel` and `cap`. The runner owns both
allocation layouts and the repetition count; cases cannot override them.

The benchmark retains the `road` pattern key for historical comparisons; it
uses the current Trail terrain identity with the same movement cost.

Cases cover classic terrain, uniform Trail/ice, sparse/connected trails, mixed
terrain and enclosed modifiers; all seven swimming profiles; dense/deferred
seeds and capped propagation; thin and rectangular tori; and synthetic registries
of 8, 32 and 64 identities with equivalent or distinct movement costs. The real
registry is measured separately. Synthetic registries call the generic prepared
profile API; they do not add game terrain definitions. `--bucket-count 256`
is an isolated future-cost experiment that changes only copied headers.

The original general bucket function is adapted only to accept the registry
extent and a distinct name. It shares queue storage types and field constants
with the candidate, so these timings isolate relaxation changes; compare full
baseline/candidate game binaries when changing those shared components.
Independent heap oracles check engine fields and strategic distances outside the
timed region.

| Mode | What it measures |
| --- | --- |
| `terrain` | Both general engine kernels, including prepared cost classes and eager uniform-cost selection. |
| `dispatch` | Production dispatch for the real registry, including the classic fast path. |
| `plane` | General propagation through a precomputed cost-class plane; construction is reported separately. |
| `strategic` | AI travel fields against the original heap implementation. Report these separately from engine gradients. |

Travel modes 1, 2 and 3 mean walking, amphibious and flying. Production dispatch
and strategic travel use the real terrain costs, not synthetic distinct costs.
Strategic fields do not have an engine propagation cap or deferred seed costs.

Samples alternate implementations in one process, using both shared and separate
output/workspace allocations. Repetition -1 measures fresh queue storage; warm
samples retain capacity. Initialization, profile preparation, class-plane
preparation and snapshot copying are reported separately from propagation.
Preparation/snapshot timings are illustrative single constructions, not stable
microsecond-level comparisons. AI propagation includes its internal allocations,
wide-distance initialization and final rounding. This harness does not reproduce
Map seeding, worker publication or production lazy-search scheduling; validate
those with the integration harnesses and whole-game traces.

Use a second `--instrumented` run for popped/stale entries, successful relaxations,
occupied layers, reservation calls and allocation counts. Its allocator and
counter hooks change timing: never use instrumented times for speed claims.
Memory output separates caller input/output, prepared profile/plane, workspace
object, retained queue capacity, AI-local queue/cost-table objects, and the maximum
additional live heap bytes during each call. Compiler stack frames and register
spills are not measured. Cold separate-workspace samples show each algorithm's own
capacity; shared warm samples inherit capacity from both implementations. Global
allocator accounting covers ordinary `new`/`new[]` allocations used by these
kernels, not process RSS or unrelated engine memory. Zero counters in the
uninstrumented build mean unmeasured, not zero work. Keep timing assertions out of
routine CI; attach raw measurements and simulation checksums to the PR. The
runner's adapter and sampling contracts can be checked without a compiler:

```sh
python3 tools/test_gradient_benchmark.py
```

Before accepting an optimization, include preparation and allocation costs in the
comparison, inspect individual scenarios as well as aggregates, and validate
whole-game behavior with identical initial states and orders. Compare every tick
across serial and parallel workers, including save/load continuation. A standalone
kernel gain is not sufficient evidence of an integrated game improvement.

The production resumable-search benchmark is separately opt-in after building
unit tests:

```sh
python3 test/run_tests.py --binary unit --no-display \
  --filter 'production lazy gradient phases*' --tag benchmark --verbose
```

It exercises nearby, distant and unreachable requests across classic, connected
road, dense mixed, uniform road and uniform ice maps at 32², 128² and 512² for all
swimming profiles. CSV layout values 0–4 follow that order; query values 0–2 mean
nearby, distant and unreachable. Rows separate initial snapshot construction,
search initialization and resolution. The same initial snapshot timing is repeated
for each row of its map and must not be summed as per-query work. Repeat zero
starts with cold queues and later repeats retain search capacity.
Every requested result is checked against the independent heap oracle. Run this
on both revisions with matching inputs and compare it separately from full-field
propagation; ordinary test runs exclude the benchmark tag.


## Local conventions

Preserve other contributors' work; use an isolated checkout when work overlaps.
Use PascalCase C++ filenames (not snake_case) and `#pragma once`. The map generator tree
(`src/map/generator/`, its tests and the lobby's landscape picker) is kept in the style
`.clang-format` at the repository root describes: tabs, Allman braces, 100 columns. Run
`clang-format -i` on the files you touch there (`pip install clang-format` gives a current
binary), and `clang-format --dry-run -Werror` over the tree to check it. Case-only renames
on macOS need an intermediate filename, e.g. `git mv Foo.cpp temp.cpp` then
`git mv temp.cpp foo.cpp`. Keep comments terse and about the current code; put change
history and rationale in commit messages; omit tombstone or “moved to” comments.
Diagnostics use `std::cerr`; there is no logging facility to target.
Windows headers define `near`, `far` and `small` as macros, so never name an identifier
after one: mingw expands `int near[3]` to `int [3]`, which then fails as a structured
binding declaration, and the error points at the syntax rather than at the macro.

For unused-include cleanup, generate `compile_commands.json` and use
`tools/remove-unused-includes.py`; do not apply blind bulk fixes. Rebuild client,
server and affected tests, then verify behavior-preserving simulation changes as above.


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
python3 tools/dev_environment.py paths --json
python3 tools/dev_environment.py paths --field android_sdk
python3 tools/dev_environment.py status
python3 tools/dev_environment.py prune --dry-run
python3 tools/dev_environment.py prune
python3 tools/dev_environment.py migrate --dry-run
python3 tools/dev_environment.py migrate --apply
```

Managed setup/build processes check the aggregate cache budget after exit, at most
once daily. The default soft ceiling is 15 GiB for regenerable caches and
completed dependency bundles. Cleanup evicts least-recently-used idle entries;
active entries are protected by OS-held leases. Gradle uses its native seven-day
unused-resource cleanup, and opted-in ccache builds default to a 4 GiB ceiling.
If active resources prevent meeting the aggregate budget, cleanup reports the
remaining pressure and retries on a later run. Installed toolchains, external
override caches, checkout build outputs, simulator data, and `artifacts/` are
outside automatic eviction. Status reports their sizes separately.

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

## Scene renderer

Drawing reads an immutable `Scene` (`src/render/scene/`), never live simulation objects, so it
runs while the simulation advances on another thread. `Game::drawSceneMap` accepts
an extracted Scene directly; `Game::drawMap` supplies extraction for legacy callers.
Offline `--render-game` and Maxima field PNGs use the same passes through a scoped,
bounded software target. Asset loading is shared with normal game startup.

- `SceneExtractor::extract(game, request, scene)` (`src/render/scene/SceneExtract.cpp`) is the
  only place presentation code reads the game. `GameGUI::drawAll` extracts
  `frameScene` once per frame and publishes it in `Game::ViewState::scene`; `drawMap`
  callers without a published scene (menu colony, editor, torus without a GUI, tests)
  get one extracted into `ViewState::render.ownScene`. `ViewState::drawnScene()`
  returns whichever was drawn.
- `SceneMap` copies the per-tile layers whole (terrain, resources, occupancy, discovery
  and fog, displayed areas); its queries match `Map`'s. `SceneEntities` holds
  presentation copies of units, buildings and flags with lookup by gid (field names
  follow `Unit`/`Building`; `team` indexes `SceneEntities::teams`), per-sector
  bullets and animations, and the selected building's map-view data. Static
  definitions (`BuildingType`, `Race`) are referenced, not copied.
- The overlay map is computed during extraction and shared as an immutable snapshot;
  it refreshes when the requested type or team changes and once per 25-tick window.
- Adding something drawn on the map: extract what the drawing needs in
  `SceneExtract.cpp` and read it from the `Scene` in the render pass. Never read
  `Game`, `Map`, `Team`, `Unit` or `Building` state from drawing code.
  `test/build_system/test_scene_boundary.py` rejects live entity reads in the render
  passes, the minimap and `GameGUIDraw*`, and simulation includes in `src/render/scene/` headers.
- Selection panels, the HUD, the top bar, statistics pages, the minimap and the building
  tool's placement preview also draw from the Scene (`ScenePanels`, `SceneMap`). Input
  handlers still act on the game, and validate against it before issuing an order.

### Target render FPS

**Settings > Display & graphics > Advanced graphics > Target render FPS** sets a
local drawing ceiling for games, replays, menus, dialogs and the editor. Presets
are 25, 30, 60, 90, 120, 144, 165 and 240 FPS, plus Unlimited. The default is
60 FPS, including profiles without the new `targetRenderFps` preference; `0`
means Unlimited. Unsupported or malformed values load as 60. Changes apply at
once and are independent of graphics detail presets. The previous threaded
native game loop capped drawing at approximately 120 FPS. The lower default
reduces rendering work and can reduce camera and interpolated animation smoothness.

`RenderFramePacer` uses nanosecond drawing-start deadlines associated with the
graphics context. Screen hosts skip painting while still dispatching input,
advancing jobs and servicing game/network updates. Browser hosts paint separately
on animation-frame callbacks, including while timer-driven jobs are running.
Sub-frame scheduling jitter retains the target clock phase; a missed full frame
discards the backlog. Foreground resume and graphics recreation
reset pacing. Display refresh, rendering cost and existing slower screen cadences
can keep the actual rate below the selected ceiling. Unlimited removes this
limiter, without overriding display synchronization or screen update scheduling.

Simulation speed, save/replay formats and network contracts are independent of
this preference. Headless runs, offline image exports and renderer benchmarks do
not opt into the interactive limiter. Recording output FPS remains independent;
recordings cannot gain new visual detail from frames the application did not draw.

### Simulation thread

Interactive sessions run the simulation on its own thread (`src/engine/sim/SimulationRunner`)
on native platforms; there is no setting. Both browser runtimes, and any platform where
creating the simulation thread fails, run the same session serially (`Engine::stepSession`), which
also remains the headless default and the equivalence reference.

- The simulation thread paces itself with the speed presets and runs ticks
  (`Engine::simulationStep`: orders, network, `Game::syncStep`). After a tick, if the main
  thread has taken the previous Scene, it extracts the next one into a `SceneBuffer`
  (lock-free triple buffer), so fast-forward extracts at most once per drawn frame.
- The main thread draws the newest Scene when the render ceiling permits a frame. Work that reads or writes the game —
  input, `GameGUI::step`, consuming `ClientEvents`, checking the selection, script
  highlights — runs in `SimulationRunner::withGame`, which parks the simulation between
  ticks (immediately when it is sleeping between ticks).
- Only state both threads use is shared: `ClientRequests`' view is locked; `gamePaused`,
  `hardPause`, `isRunning` and the CPU-load history are atomics. A pause order or the local
  player leaving takes effect on the simulation thread in the same tick, as in serial
  execution. GUI state extraction reads (selection, local team) changes only while the
  simulation is parked.
- The synchronized RNG belongs to the game, so results do not depend on the thread.
  `GLOB2_SIM_THREAD=1` runs headless sessions on the simulation thread for
  `check_sim_thread.py --candidate-env GLOB2_SIM_THREAD=1`; `GLOB2_SIM_THREAD=0` keeps
  any session serial, for tests that count frames against a scripted host clock.
- The simulation thread paces on the host's clock (`Engine::sessionClock`): the clock the
  host last passed in, advanced by real time. Time the application spent in the
  background is therefore not caught up after resuming, as in serial execution.
  GUI updates use `SDL_GetTicks()` instead: touch event timestamps and momentum
  must share the SDL clock, including after the session clock has been suspended.
- Values the client sets while drawing and extraction reads (viewport, drawn map size,
  overlay, observed building) go through `ClientRequests`, never through `Game` or `Map`
  fields. To check for races, build with `CXXFLAGS="-g -fsanitize=thread"
  LINKFLAGS="-fsanitize=thread"` and run a windowed `-test-games` session or a headless
  `--run-game` with `GLOB2_SIM_THREAD=1`. Build against the pinned SDL3 prefix
  with `GLOB2_SDL3_PREFIX`; sanitizer builds use the same native SDL3 dependency set.
  `.github/workflows/thread-sanitizer.yml` runs both games under ThreadSanitizer nightly,
  through the main build workflow and on demand. The risk selector includes it
  for code shared by threads; add boundaries in `.github/scripts/ci_policy.py`
  when new code becomes shared between them. Drafts defer it. It does not report thread leaks, because SDL3
  leaves its own startup threads unjoined at exit, and uses the dummy audio driver, because
  PulseAudio's uninstrumented mainloop thread reports races inside libpulse.
  The windowed fixture explicitly uses the game's software renderer and disables SDL's accelerated
  framebuffer presentation, keeping uninstrumented Mesa worker threads out of the sanitizer run.
  GPU rendering remains covered by the renderer suites. Narrow, explained suppressions for
  library shutdown races live in `test/tsan.supp`; never suppress game code there.
  Draft PRs skip it.
- `SceneBuffer<T>` (`src/render/scene/SceneBuffer.h`) hands Scenes between the threads without
  either waiting for the other.

### Smooth unit motion

The experimental **Smooth unit motion** graphics setting (`Settings::unitInterpolation`,
off by default) draws units between ticks, so threaded play at display rate uses all
32 animation frames per direction instead of repeating one pose per tick.

- A unit's drawn position and animation frame follow `delta`, which the simulation
  advances by `SceneUnit::stepSpeed` each tick. Each frame, `GameGUI::drawAll` sets
  `MapRenderState::unitMotion` to the elapsed fraction of the tick interval since the
  Scene's tick (`Scene::tickTime`, `Scene::tickInterval`; `src/unit/render/UnitMotion.h`).
  Unit drawing, path lines, off-screen markers and worker circles add that fraction of
  `stepSpeed` to `delta`, stopping at the end of the current action.
- Motion is 0 when the setting is off, when the game is paused, and when the simulation
  runs uncapped. At 0, drawing is identical to drawing the ticked state; keep it that way
  so captures with the setting off stay comparable.
- A unit that turns or stops at the next tick can jump back by at most one tick of motion.
  Serial execution draws right after each tick, so the setting has almost no effect there.

### Smooth fog of war

The simulation's fog of war is binary per tile, and its buffers swap every
`FOW_SWITCH_TICK_MASK + 1` ticks (`Map::switchFogOfWar`), so every tile that left sight
during one window darkens on the same tick. The **Smooth fog of war** graphics setting
(`Settings::smoothFog`, on by default) fades that change in the renderer only; the
simulation, saves and replays are unaffected.

- `FogFade` (`src/render/FogFade.h`), kept per view in `MapRenderState`, records for each
  tile whether it is fogged, its fade level when that last changed and the tick it changed
  on. `Game::drawMap` updates it once per frame from the drawn Scene, and resets it while
  the fade is not drawn (setting off, or `DRAW_WHOLE_MAP`), so it is `active()` exactly when
  the frame draws the fog faded. A tile changing state fades linearly from wherever it had
  reached, into the fog over `FogFade::DARKEN_TICKS` (37.5, or 1.5 seconds at normal speed)
  and out of it over `FogFade::REVEAL_TICKS` (4). A new map, other visible teams, a step back in time, a jump
  forward of more than `FogFade::SETTLE_JUMP_TICKS` (64) or a reset settle every tile
  without fading.
- Fades run in game time: the Scene's tick plus the elapsed fraction of the tick interval
  (`unitMotionFraction`), independently of the smooth unit motion setting. They stop while
  paused and follow the game speed.
- The shade draws each square from its four corner levels. The level all corners reach is
  a fill (`FogFade::fillAlpha`); each higher corner level adds the shade sprite masked to the
  corners reaching it, drawn with `FogFade::layerAlpha` of the difference. The alphas are
  chosen so the layers compose to the fill of the top level (within rounding, where the
  sprite's pixels are at their peak; the derivation is in `FogFade.cpp`), and fully fogged
  or fully clear corners draw the same single fill or sprite as with the setting off.
- Enemy units fade with the clearer of their tile and the tile they come from, and so does
  everything drawn for them: bars and status pips (through the `opacity` parameter of
  `Game::anchorBars`, `drawStatusPip`, `drawPointBar` and `drawHealthBar`; queued bars take
  it from their anchor), selection circles, the level-up number and magic effect, the
  carried resource and the accessibility label. A fading unit's sprite is translucent and so
  leaves the unit sprite batch; only the few units at the edge of the fog do.
- Still binary: undiscovered black, the minimap, mouse and touch picking, bullets and
  explosions, and remembered enemy buildings. Bullets near a fading unit therefore vanish
  at the fog swap.

## Adaptive zoom detail

The map zooms from the fitted whole map up to 500% (`MapCamera::MAX_ZOOM`). With the
`adaptiveZoomDetail` graphics setting (default on), map elements change
representation with the zoom instead of scaling uniformly. Presentation only: none of
it is saved, checksummed or read by the simulation.

- `ZoomDetail::forView` (`src/render/ZoomDetail.h`) is the single source of the
  curves. Its input is the size of one tile in screen points
  (`32 * zoom / logicalUnitsPerPoint()`), so thresholds mean the same on a phone, a
  high-density display and a desktop. Every threshold is a named constant there; each
  ramp is a smoothstep between two tile sizes, so representations cross-fade.
  `Game::drawMap` computes it once per frame into `MapRenderState::detail`.
  The map has two looks, the detailed map and the strategic overview. Everything
  that differs between them (terrain, unit and building sprites, flags, zone
  outlines, status pips, the territory wash) cross-fades inside one narrow window,
  from 9 down to 7 points per tile, so each look is undistorted over a range of
  zooms either side of it. Keep new representations on that window.
  A small map cannot zoom out far enough to reach the overview by tile size,
  so the view's owner sets `MapRenderState::minimumZoom` and `ZoomDetail::rampTile`
  remaps the far range: fully zoomed out is the full overview on a map of any size,
  it holds up to 1.5 times that tile size (at most 16 points), the cross-fade above
  it is as narrow as on a large map, and from normal size in nothing changes. A
  map still at 20 points or more per tile when fully zoomed out is left detailed.
  Overlay sizes always follow the true zoom. Disabled,
  it returns the values of uniform scaling and the passes take their original paths.
- `MapOverlayQueue` (`src/render/MapOverlayQueue.h`) holds overlays of constant screen
  size. Passes queue them at a map position while the map transform is active;
  `drawMap` flushes once after the air units (under clouds and fog) and once on
  return (flags), inside one `beginScreenOverlay` scope. Bars therefore draw above
  every unit and building rather than interleaved with them. `Game::anchorBars` names
  the map point a bar keeps fixed while its size changes.
- Bars hold their 100% size from 48 down to 20 points per tile and change slowly
  outside that. Between 20 and 16 points every bar fades together. From there down
  to the overview a status pip marks what needs attention (a starving unit or one at
  60% health or less; a damaged building, one with under half its workers, an inn
  without food, a tower without ammunition).
- Zones cross-fade from pattern sprites with an outline to a flat translucent tint
  without one: an area keeps its shape at any scale where a one-pixel line cannot.
  `GraphicContext::drawMapFill` snaps fill edges to target pixels so translucent
  neighbours tile without seams. The fog-of-war shade draws one fill per horizontal
  run of whole squares at one fade level with `drawMapTileFill` and its edge sprites
  with `drawMapTileSprite` (see [Smooth fog of war](#smooth-fog-of-war)). Both snap to pixels only in the software rasteriser, where
  truncated coordinates otherwise leave one-pixel gaps between tiles; accelerated
  renderers place sprites at exact fractions, and a snapped fill beside them
  leaves hairline seams.
  The outline stroke stops thickening at two points.
- In the cross-fade `Game::drawMapOverview` fades in terrain palette colours sampled
  from the detailed compositor's material coverage, including legacy corner shores
  and whole-cell materials. Four samples per tile axis keep coastlines aligned
  during the fade; resource minimap colours tint their gameplay cells over that
  ground. In the overview this replaces the water, terrain and resource passes.
  The reusable image stretches over the map in a single draw, avoiding thousands
  of translucent fills.
- Units cross-fade to team-coloured markers (dot worker, triangle warrior, diamond
  explorer). Bullets, explosions, death animations, the magic effect and the
  level-up number go with the unit sprites. Building sprites cross-fade to chips in the
  team's colour carrying a white icon of the building's purpose, with a pip per
  upgrade level and a paler chip for a construction site; flags become discs of
  constant size; walls become plain team-coloured tiles. Chips are 15 to 26 points
  and placed in priority order (damaged, flags, towers, hives, the rest); one that a
  placed chip would cover by more than 15% is left out. The icons are
  generated from `data/gfx/mapicon*.png` sources and loaded as WebP in the runtime
  tree. The sources are rasterised at seven pixel sizes from the SVGs in
  `datasrc/icons/map/` by `python3 tools/icons/export_map_icons.py` (needs
  `rsvg-convert`); the renderer draws the largest frame that fits, pixel for pixel.
  Frame order is shared between that script and `MapOverlayQueue.cpp`.
- In the overview `Game::drawMapTerritory` washes the land around each team's
  visible buildings in its colour, inside a solid border two points wide. The
  team colour is brought up to a common brightness first, so a dark colour reads
  as well as a light one. An under-attack event raises the same pulsing mark as a
  player's ping.
- The torus view draws its map texture through the same map transform at the
  camera's zoom (`TorusView::draw`), so it shows the same detail, overlay sizes and
  overview as the 2D view at that zoom. Its tiled atlas chooses terrain sampling
  density from the complete map capture, then admits each bounded tile at that
  density. Narrow edge tiles therefore reuse warm pages and keep the same shore
  samples as their wider neighbors, including when HD sources exceed the cache
  budget. Streaming fallback follows the same density choice.

When tuning, capture the same save across zooms with `SoftwareRenderBenchmark`
(`PROFILE_ZOOM`, `PROFILE_CAPTURE`); `PROFILE_ADAPTIVE_ZOOM=0` draws uniform scaling
from the same build for a before/after pair. Set it explicitly on every run: the
benchmark saves preferences, so the last value otherwise carries into the next run.
`torus-render-benchmark` takes `GLOB2_BENCH_ZOOM`, `GLOB2_BENCH_PAN_X`/`_Y` (the
camera's top-left tile), `GLOB2_BENCH_AREAS=1` (zones), `GLOB2_BENCH_FOG=1`
(with `GLOB2_BENCH_SMOOTH_FOG=0|1` for the fade),
`GLOB2_BENCH_FRACTION` (a camera offset in map pixels, which seams need to show) and
`GLOB2_BENCH_ADAPTIVE_ZOOM=0|1` for the same comparison on OpenGL. Buildings,
including custom swarm artwork, fade out as their icon chips fade in. Unit sprites
stay opaque while markers fade in over them. Check draw calls as well as time
when changing a cross-fade, since a translucent sprite can leave its batch.

## Software rendering architecture and profiling

`GraphicContext` remains the drawing facade and retains existing capability queries.
It owns the accelerated backend and software backend independently; transformed passes
borrow them through scoped transform/clip state (`RenderStateScope.h`). The CPU backend
in `SoftwareRenderBackend.cpp` implements verified same-format opaque sprite blits and
opaque rectangle fills directly on its borrowed framebuffer. Translucent draws and
mixed pixel formats retain SDL geometry rasterization so platform-specific blending
rounding and source modulation match the reference. General triangles use that same
lazy SDL renderer; its queue flushes before direct writes or target replacement.
Large existing images expanded past 512 pixels, including water, retain SDL geometry
rasterization because its fixed-point overflow behavior is visible at some transformed
sizes. Borrowed terrain run views use direct rasterization: they replace small tiles and must not acquire that
large-triangle behavior. Correcting the legacy large-image appearance needs separate
visual acceptance.
`RenderBackend.cpp` contains the accelerated SDL implementation and its texture uploads.

`SurfaceRaster.cpp` owns pixel arithmetic. Unscaled sprites use opaque copies only
when their pixels are verified opaque and draw opacity is 255; other sprites use the
conservative blending path. Native drawing preserves the existing draw-opacity arithmetic. Fully unclipped native
scaling uses SDL's optimized nearest scaler without classifying mutable UI surfaces.
Clipped and transformed blits sample nearest source pixel centers from
the original destination rectangle; clipping cannot change sampling. Transformed
rectangles round both endpoints with `floor(edge + 0.5)` and derive their size afterward,
so adjacent tiles share a boundary at fractional zoom. This can change fractional-scale
sampling and boundary placement by one output pixel. Source blend/alpha modulation is
restored after each operation. Transformed primitives preserve SDL triangle blending
rounding, including independent source/destination truncation for textured draws. Native
rectangle alpha arithmetic retains the legacy `/256` rounding. Sprite modulation uses
exact `/255` arithmetic and zero-alpha sprite pixels leave the destination untouched.

Surface content revisions are independent of texture upload revisions. Each accelerated
backend tracks its own uploaded revision; opacity classification is cached against the
content revision. Code that edits pixels through `getSDLSurface()` must call
`markPixelsChanged()` afterward. This includes raw SDL copies and external rasterizers.

`GameRenderFrame` groups the viewport, assets, visibility and draw options inside the
existing game rendering entry point. Presentation state a view keeps between frames —
animation phases, the cloud field, the overlay scratch buffer and the software terrain
cache — lives in `MapRenderState`, owned by `Game::ViewState`, never on `Game` or `Map`;
the simulation neither reads nor writes it and each view animates independently. The
terrain cache is transient presentation state: 16×16-cell composed pages, a 32 MiB
software storage reservation including pixels, recipes and borrowed views, and a
separate 128 MiB GPU-mode reservation with least-recently-used eviction. Native and
HD rendering share CPU composition; GPU backends upload the resulting pages.
The [terrain authoring guide](../assets/terrain-materials.md) describes the catalog,
boundary resolver, source preparation, budgets and asset pipeline.
Its deterministic world-space displacement continues contours across tiles at
three scales, with shared wrapped control points and a bounded local contour
budget. Prepared tiles hash control points once; pixel sampling interpolates them
at the requested native/HD resolution. This changes coverage only, not terrain
identities, texture selection or simulation randomness.

Within a software page, adjacent opaque tiles become borrowed surface views over
the raw pixels. Fully transparent tiles submit no draw. Partially transparent
coastlines retain individual source blits, avoiding repeated alpha scans over
transparent holes. Views are destroyed before their backing page.
Each page validates the canonical terrain neighborhood, discovery decisions and
revisions of the materials its recipes use. Animation or source changes in unrelated
materials do not invalidate it. Pages store raw color/alpha, so coastlines blend over
animated water once. Map replacement (a new `Map::identity()`) clears the cache;
editor terrain changes, wrapped neighbors and visible-team changes are detected
during preparation. Resources, actors, fog and overlays keep their existing
passes. Water coverage subtracts only verified opaque terrain rectangles, including discovery
boundaries. A complete animated water tile is omitted only when all of it is covered;
partially covered tiles retain their original source mapping and animation phase.
Coverage includes the original water pass's overshoot outside the viewport, which a
transform can bring onscreen. Fragmented coverage falls back to the full pass after
64 rectangles. Oversized working sets stream one temporary canonical page at a time
at the same sampling density as the full view. If a page cannot fit the device or
allocation fails, an emergency composed-tile path preserves coverage but can differ
in fractional resampling and HD mip filtering. None of these caches enter saves,
simulation checksums or orders.

`SoftwareFramePresenter` owns two framebuffers and retains the completed one for exposure
repaint. `beginFrame(FullRedraw)` rotates without a retention copy. Partial updates,
including legacy callers that begin implicitly on their first drawing operation, copy
the completed frame into the next drawing target. Target rotation flushes queued work
and rebinds the software backend. Resize retains the old completed image until the first
replacement frame completes. Letterboxing and minimized-window handling remain in the
window presentation boundary; failed spare-buffer allocation uses the prior frame-cache
copy path. `completedFrame()` provides the retained software image; normal screenshot
requests continue to capture the current drawing frame.

Build the opt-in saved-game benchmark with optimized production objects:

```sh
scons release=1 server=0 opengl=0 software-render-benchmark
PROFILE_SAVE=artifacts/software-renderer/initial.game.gz PROFILE_ZOOM=0.5 \
  PROFILE_FRAMES=240 PROFILE_WARMUP=30 PROFILE_NO_PRESENT=1 PROFILE_CPU_SCOPES=1 \
  GLOB2_USER_DATA_DIR=artifacts/software-renderer/profile \
  build/darwin/client/release/test/SoftwareRenderBenchmark -G -s 1280x800 -m -F
```

Use the appropriate `linux`/`mingw` build directory or an explicit `--build=DIR`.
Resolution is the existing `-s WxH` argument, measured in framebuffer pixels.
The benchmark creates its SDL3 window without high-density backing pixels by default,
so the workload does not change with monitor density. `PROFILE_NATIVE_DISPLAY=1` retains native Retina/HiDPI presentation. `PROFILE_OFFSET_X/Y` add logical-pixel camera
offsets; `PROFILE_FRACTION=1` adds a half-pixel horizontal offset. `PROFILE_VISIBLE=1`
shows the window; omit `PROFILE_NO_PRESENT` to include presentation. `PROFILE_CAPTURE`
names an output BMP. `PROFILE_TERRAIN_CACHE=0` isolates primitive performance without
adding a user graphics setting. `PROFILE_SELECT=building|flag|unit` selects the local
team's first such entity, so frames include its selection panel and map markers;
`PROFILE_TOOL=<building type>` (for example `inn`) activates the building tool with the
cursor over the middle of the map view, so frames include the placement preview. Use them
with `PROFILE_MODE=gui` for Scene parity captures against another revision. The harness reports population, wall-time mean/median/p95,
process CPU time, optional thread CPU stage costs, backend operation counts, cache memory
and cache hit/rebuild counts. It also checks that drawing preserves the simulation checksum.
Run captured fixtures from early, mid and late games; keep generated saves and profiles
under ignored `artifacts/`. To advance a saved initial game into population fixtures,
use the existing structured runner with its saved seed and orders, for example:

```sh
GLOB2_USER_DATA_DIR=artifacts/software-renderer/fixture-profile \
  build/darwin/client/release/src/glob2 --run-game \
  --load-game "$PWD/artifacts/software-renderer/initial.game.gz" --ticks 12000 \
  --save every:6000 --save final --telemetry checksums \
  --output-dir "$PWD/artifacts/software-renderer/populated"
```

Keep the initial save, generated checkpoints and runner metadata together. Fixture
population matters more than the tick label; a late game can have fewer surviving units.

For paired measurements, preserve a baseline benchmark executable before rebuilding and
run at least seven alternating pairs on the same fixtures, resolution and hardware:

```sh
python3 tools/software_render_benchmark.py \
  --baseline artifacts/software-renderer/baseline/SoftwareRenderBenchmark \
  --candidate build/darwin/client/release/test/SoftwareRenderBenchmark \
  --save artifacts/software-renderer/initial.game.gz --save artifacts/software-renderer/mid.game.gz \
  --save artifacts/software-renderer/late.game.gz --repeat 7 \
  --output artifacts/software-renderer/comparison
```

The runner records raw logs/captures, exact commands and CPU distributions for native,
half, double and fractional-offset scenarios. `--no-terrain-cache` now streams
composed pages without retaining them between frames; it measures repeated
composition and upload, not the old sprite-only terrain primitives. Compare the
same binary with `--baseline-no-terrain-cache` to isolate retained-page caching.
Use `--present --visible --scenario native --baseline-preserve-frame` with the same
binary to measure the retention-copy savings. `PROFILE_PRESERVE_FRAME=1` begins each
benchmark frame in preserve-content mode before the full redraw. Keep other heavy
work off the measurement machine. On macOS, `sample PID SECONDS -file artifacts/profile.txt`
can identify CPU stacks; Linux `perf` and Windows profiling tools can sample the same
opt-in executable. Timing thresholds are review criteria, not CI assertions. Run
`SoftwareRenderer`, `PortableRenderer`, `WindowResize`, `MapRenderResize` and
`HighResolutionIntegration` suites on supported SDL/platform builds, retain before/after
captures, and report unavailable platform and maintainer-playtesting coverage explicitly.

### CI timing and retained revisions

Pull requests cancel superseded revisions and run cheap contracts by default.
Master finishes active full verification and keeps the newest pending push; all
retained master pushes select the full development matrix. Nightly fallback runs
at 06:00 UTC in a separate concurrency group without publication operations, and
skips expensive jobs only with matching available successful full evidence for
its exact revision and policy.
Release packaging first runs the full development matrix on the exact candidate
revision through `build.yml`'s `revision` workflow-call input.

CI measurements batch completed runs hourly using trusted default-branch code and
inert artifacts. Cancellations do not create measurement workflows, and cheap-only
observations are excluded. Explicitly requested verification on drafts is included.
Batches retain attempt identities to avoid remeasuring
completed runs and attempt at most ten new measurements per batch. `feedback.json` reports p90 after ten successful matching-inventory
PR samples, with explicit gaps when there are fewer samples. Metrics report queue delay, active execution time, idle
gaps, aggregate runner minutes, feedback time and cache observations separately.
Per-job queue timestamps are estimates, not proof of runner saturation. Overlapping
jobs count once in wall execution time and separately in runner minutes.
Compare ten successful runs with matching event and exact coverage inventory using
`python3 .github/scripts/ci_run_metrics.py --before before.json --after after.json`.
Report workload reductions separately from execution savings. The initial service
objective for explicitly requested affected-PR verification is p90 feedback below
15 minutes and queue delay below two minutes; full verification and releases may
take longer. Cheap-contract feedback is not engine verification feedback. Missing
samples and missing inventories cannot establish improvements.

### Linux execution dependencies

GCC 13 builds its client and applicable transport programs once. Runtime consumers
reuse same-run artifacts with matching source/configuration provenance. Compiler
build jobs publish programs before separate CLI/scripting regressions; native and
browser consumers can start without waiting for those regressions. Golden-only
changes build once per selected platform and distribute programs to sweep consumers.
Primary generator sweeps are complete; secondary platforms retain golden rows and
telemetry equivalence under enabled tiers. Native inventory audits require every
selected engine case to belong to exactly one shard.

ELF dependency collection records runtime package owners and rejects unresolved or
unowned libraries. Generator, CLI and browser consumers install runtime libraries
rather than compiler/header environments. Enable `CI_RUNTIME_PACKAGES_ENABLED=true`
for ordinary engine shards only after `Validate clean Linux runtime images` passes
both Ubuntu container images. Until then engine shards retain their prior package
installation; the clean-image dispatch also exercises CLI/scripting consumers.

Pinned SDL3, WebAssembly and Android dependency prefixes are cached independently
from application objects. Restored prefixes require matching inputs and complete
file hashes; invalid content rebuilds. Android tooling and Playwright installations
use pinned tool/package identities. Default-branch jobs publish shared caches;
ordinary PRs restore them. The cleanup sweep removes closed-PR caches and retains
three generations per master compiler/object family. Cache timings and quota
pressure must be measured before expanding retained cache data.

### Reviewed native shard timing profiles

Native runners optionally accept `--timing-profile` and `--auxiliary-jobs`.
Nonempty profiles assign jobs longest first to the least-loaded shard, with label
and shard-number tie breaks; unknown cases use the median recorded duration.
Empty or omitted profiles preserve alphabetical slicing and existing auxiliary
ownership. Unit tests and auxiliary groups participate in the same load plan,
retaining their original commands, flags and timeouts. Every selected engine job
runs exactly once. Auxiliary artifacts follow their assigned group.

Successful job durations are retained alongside JUnit results. To propose updated
weights, gather observations and run
`python3 test/build_ci_timing_profile.py observations --family ubuntu-24.04 --output test/ci-timings/ubuntu-24.04.json`.
Only platform-matched jobs with ten successful samples enter a profile. Review the
resulting diff before shipping it; weights never change during a run. Profiles
remain empty until measurements are available, rather than using invented data.

### Local and VM PR verification

Relevant local or VM testing is the standard PR verification path. Maintainers
accept evidence directly, including their own evidence; hosted CI success is not
a merge prerequisite. Choose tests from the change's actual risks and justify
coverage and omissions rather than reproducing the CI matrix. A VM supplies
coverage for its actual OS, architecture and configuration; testing on one platform
does not establish another platform's compatibility.

Record evidence in a PR comment using this template, with links accessible to
reviewers. Store generated files under ignored `artifacts/` and temporary narratives
under `docs/.work/`, then upload or attach evidence for review; local paths alone
are insufficient. Keep secrets out of uploaded logs.

```markdown
Local / VM verification

- Tested commit SHA:
- Base revision and integration state (PR head or merge with base):
- Environment: OS, architecture, VM/container image if applicable, compiler/runtime versions:
- Dependencies, build configuration and flags:
- Coverage rationale: changed behavior and risks addressed:
- Exact build/test commands and results (including counts and exit status):
- Omitted checks and why; limitations:
- Evidence: accessible links to logs and applicable checksums, saves, replays or screenshots:
- Maintainer acceptance: sufficient evidence for this revision, accepted by <name>:
```

Evidence must describe the tested source and binaries; reuse built artifacts only
when source, compiler, flags and dependency inputs match. Refresh evidence when
later edits affect tested behavior, dependencies or integration. Fetch current
master before final validation and resolve actual conflicts. Unrelated master
advancement alone does not invalidate evidence; changes in the same components,
dependencies or CI configuration require renewed integration assessment.

Focused coverage does not waive affected simulation determinism, save/load,
replay/network, platform compatibility or simulation-version requirements in
`AGENTS.md`. Local and hosted results may jointly supply that coverage. Hosted
checks may be pending or unavailable when merging; known failures introduced by
the PR still require resolution.

### Hosted verification and regression detection

Draft and ready PRs run the existing cheap contracts by default. Changes under `tools/music/` also run the cheap `music` job (the pipeline's
Python unit tests in a venv from `requirements.txt`), on PRs without `ci:run` too; it
gates no engine verification. Community converter changes also select platform and
stack verification; shared WASM exports/build inputs select full verification
when hosted checks are requested. The music job includes a small C++ portable-file
round trip using libopusfile. Files inside a soundtrack set, `data/zik/<set>/`, select
the native and browser checks when hosted verification is requested, while
`data/zik/SConscript` stays on full CI. Becoming ready
starts no expensive jobs. `ci:run` requests hosted affected checks; `ci:full`
requests the complete development matrix, even in draft. `ci:windows`, `ci:android`
and `ci:browsers` expand requested coverage but do not start verification alone.
Label changes re-evaluate selection; removing the request labels restores
cheap-only selection. The aggregate summary rejects missing, failed, cancelled
and unexpectedly skipped selected jobs, while clearly distinguishing cheap-only
success from engine verification or acceptance of PR evidence.

Every retained master push runs the full development matrix regardless of tier
settings. Active runs finish and only the newest pending push remains. Full master
CI detects regressions asynchronously; existing master failures do not restrict
PR merges. Retain failure artifacts, prioritize diagnosis and repair, document
verification in repair PRs and confirm recovery with subsequent full master runs.
Do not require master to become green before other PRs merge.

Nightly is a fallback with a separate concurrency group. Expensive nightly jobs
are skipped only when a successful full hosted run already covers the exact master
SHA under the current coverage policy and its evidence is available. Missing,
expired, mismatched or inaccessible evidence triggers the full matrix. A skipped
nightly is not a new full checkpoint. Manual and release verification retain their
existing behavior. Local evidence never substitutes for a hosted full checkpoint.

### Tiered pull-request coverage rollout

`.github/scripts/ci_policy.py` records proposed and effective selection, reasons,
changed paths, policy identity and selected command inventory in `ci-selection.json`.
The observation also identifies `cheap-contracts`, `affected`, `full` or
`nightly-reused` verification and the reused run ID when applicable. Native runners
retain eligible/assigned case inventories. Cheap-only PR runs do not enter ordinary
verification performance cohorts; requesting tests on a draft does not exclude
actual verification from those cohorts.

For explicitly requested affected PR verification, primary Linux keeps the
complete applicable native suite. Simulation/save/AI changes add older-GCC and
Windows compatibility cases plus native/browser per-tick and scripting comparisons.
Presentation changes retain software/WebGL and Firefox/WebKit coverage. Network
changes retain transport/server/deployment checks. Android changes retain arm64
builds and x86_64 emulator smoke. Shared headers, dependency/build configuration
and unknown paths select the full development matrix.
`test/ci-compatibility.json` owns repeated native compatibility cases; add suites
there when introducing a portability boundary.

Affected-PR tier reductions remain disabled until a full hosted master/nightly
matrix validates the current policy. `CI_TIERED_COVERAGE_ENABLED=true` activates
them with matching available full evidence; `CI_TIER_BASELINE_RUN_ID` may specify
a preferred baseline. Missing evidence or setting the flag false restores
conservative affected-PR coverage. These settings never reduce master coverage or
start expensive PR checks without an explicit request.

## Untrusted maps, saved games and replays

Treat serialized fields as untrusted before using them as indices, allocation
sizes or runtime state. Map/save game and GUI decoding use checked binary reads;
a short field raises an error instead of supplying partial or zero-filled state.
Keep these scopes around the entire load, including legacy fields. Callers must
handle both a false result and a decoding exception, and must not run a partially
loaded game. Map headers validate their team records, and entity loaders validate
types, identities, levels and cross-references before using them. Map sector
dimensions must match the dimensions derived from the terrain grid.

Names from headers are converted to a single portable filename component before
download/save paths are built: separators, controls (including NUL), Windows
reserved characters and device basenames cannot redirect writes outside the
selected directory.

Gzip file loaders limit compressed input to 256 MiB and expanded output to 2 GiB.
The separate expansion limit permits the existing late-game snapshots (roughly
1 GiB uncompressed) while still bounding hostile compressed streams.
Scenario objectives, hints and legacy areas are limited to 65,536 records;
building/unit reference lists are bounded by the corresponding entity capacity.
AI and history collection reads are limited to 1,048,576 entries per collection,
with Maxima continuation retaining its explicit 16,777,216-entry bound for
large saved geometry tables. Nested AI condition graphs are limited to 64
factory calls. Unknown AI implementations,
object tags, invalid module indices and malformed queued orders are rejected.
These are reader limits, not a new disk format. Files exceeding these limits are
rejected. Ordinary valid saves retain their format versions and continuation
semantics.

Replays must contain a complete, valid order stream ending in a null order.
Truncated recordings are rejected, including recordings with a valid prefix;
playback no longer tries to recover a prefix from a malformed stream. Both scanning
and playback check short reads, and the scan rejects overflowing step totals.
Execution checks player/team references before indexing game state. This is not
an authentication mechanism for multiplayer commands. Voice orders additionally
limit encoded audio to 4,096 bytes and 121 frames before copying or decoding.
The recorder flushes after crossing 2,048 bytes or 120 frames, so the receive
limits retain headroom for its final frame and existing recordings.

Embedded legacy USL has no `load` file capability. The application still loads its
own runtime libraries through the host API. Native type errors stop the map script
instead of asserting or dereferencing an invalid value. USL also rejects integer
overflow, limits source to 1 MiB and 65,536 tokens, bounds parser nesting (128)
and expression chains (256), and checks runtime frames (1,024), frame operands
(65,536) and heap values (262,144) at instruction boundaries. Garbage marking
uses an iterative traversal to avoid overflowing the native stack. Legacy SGSL saved program
counters must match parser-derived statement boundaries or the integer suspension
point of `wait(N)`.

These checks do not establish a hostile-script sandbox. Script interpreters and
native simulation code still share the game process. Per-collection and
instruction-boundary limits are not an aggregate byte or execution-time budget.
Keep dependency review, sanitizer fuzzing of the complete load-and-step path,
and platform replay/checksum comparisons separate from targeted rejection tests.
See [JavaScript scripting](javascript.md) for that interpreter's capability and
resource boundaries.


## Native simulation memory and CPU comparisons

Build the client itself with `scons release=1 server=0`; the `tests` target alone
can leave an older game executable in place. Preserve a baseline with the same
benchmark instrumentation, build options and dependencies before rebuilding.
The structured runner accepts `--benchmark-warmup N` when loading a saved game.
It reports process CPU nanoseconds for setup/loading, execution after the warmup,
and the final save in `result.json`. The measured execution includes pending
pipeline completion; save compression is measured separately. `--ticks` remains
an absolute game tick, and the warmup must leave a nonempty measured window.

```sh
python3 tools/memory_benchmark.py \
  --baseline artifacts/memory/baseline/glob2 \
  --candidate build/darwin/client/release/src/glob2 \
  --fixture 0=artifacts/memory/initial.game.gz \
  --fixture 15000=artifacts/memory/checkpoint-15000.game.gz \
  --fixture 45000=artifacts/memory/checkpoint-45000.game.gz \
  --output artifacts/memory/comparison
```

Use the appropriate platform build directory. The runner alternates seven pairs,
checks identical final simulation checksums and compressed save bytes, and extends
to at most 21 pairs if the one-sided 95% paired bootstrap upper bound exceeds the
2% CPU regression limit. It retains binary/fixture hashes, commands, raw logs,
timings and one final save per variant and fixture. Keep other heavy work off the
machine. CPU comparisons explicitly disable malloc logging; collect heap profiles
in separate runs using the same fixtures. Record actual capture ticks, distinguish
live allocation totals from resident memory and allocator retention, and report
platforms whose execution could not be checked.

On POSIX, `--interleave-seconds 0.1` alternates the two processes with
`SIGSTOP`/`SIGCONT` within each pair. Process CPU clocks exclude the pauses; this
reduces drift from changing background load without changing the measured tick
window. Both games remain resident, so this is a separate scheduling condition,
not a peak-memory measurement. Retain sequential results too, and identify the
measurement condition when reporting the gate.
Use `--resume` with the same arguments to continue completed pairs after an
interruption. Binary hashes, fixture hashes and measurement settings must match;
the unfinished pair is rerun.

Memory-only representation changes must preserve legacy serialized widths and
sentinels. Maxima's obstacle-free distance fields use 16-bit storage with an
internal 65535 infinity, translated to the existing signed 32-bit `INT_MAX` on
save/load. Route distances retain their wider representation. Food source masks
retain their legacy 64-bit encoding. Wrapped nine-cell geometry tables share
immutable storage by map dimensions; loaded noncanonical tables retain their
original content. Weighted gradient searches share an immutable water snapshot;
water-classification changes invalidate the map's current snapshot while ongoing
searches retain their frozen version. Growth overlap counters use 16-bit storage;
removal precedes addition so both generations cannot temporarily exceed the bound.

Game-file persistence uses move-only snapshots in uninitialized 1 MiB blocks.
Growing the block table does not copy the saved bytes; allocated byte capacity is
at most the snapshot length plus one block (with a separate small pointer table).
Gzip loading reads compressed input in 64 KiB batches and inflates directly into
these blocks. CRC/truncation/trailing-data validation completes before loading
headers or simulation state. Filename-based custom and campaign initialization
reuse one validated input for both headers and the body, then release it before
replay/network setup. Standalone header readers retain their existing interfaces.

Interactive saves capture owned literals and bounded array/history batches at a
consistent game boundary. A lightweight fixed integer representation bounds the
capture memory; final array encoding and history transposition run on the worker.
Final output uses chunked storage, with relocated header offsets and SHA1 ranges;
headless callers can still serialize synchronously without changing saved bytes.
A captured `DeferredStream::Snapshot` is consumed once: finalization releases its
owned inputs as their output is produced. Append deferred fields in stream order;
seeks may only backpatch fixed-size literals. Serialize stream positions with
`OutputStream::writeOffset32`, which explicitly registers relocation on deferred
streams and writes an ordinary uint32 on binary/text streams. Field names do not
control relocation. SHA1 still covers the original header followed by the final
body, preserving the existing pre-backpatch hash contract.

Worker gzip compression uses bounded 256 KiB output buffers; cooperative gzip
uses 64 KiB input/output steps. Neither flushes at input-block boundaries.
Optional level-zero compression retains the legacy whole-buffer path to preserve
zlib's stored-block byte layout; it is outside the normal-save memory bound.
Background finalization does not add a wire-format change beyond compact format
128 (save floor 58, replay floor 127, network protocol 51).

For the 45,000-tick large-map fixture, the native macOS arm64 comparison measured
1.93 GiB peak RSS for combined loading and saving, down from 3.96 GiB in the
previous optimized build. The acceptance limit is 2.5 GiB for that fixture with
default compression. This is a fixture-specific measurement, not a bound for
arbitrary games or other platforms; preserve the commands, fixture hashes and
raw measurements under ignored `artifacts/` when repeating it.

Autosave defers capture while a previous writer is busy, then captures the current
tick when the writer becomes idle. It never queues a second owned snapshot or
waits for compression during a game tick. Manual game and editor saves keep their
dialog pending while waiting for the worker, writing the file, and persisting
browser storage; names and editor dirty state change only after success. Editor
mutation is disabled while saving. A pending save dialog cannot be replaced by
another panel. Normal session exit stops simulation and keeps presenting frames
and polling the dialog through queued capture, file writing and browser storage
completion. A failed save remains actionable for retry/export or cancellation;
exiting does not silently discard that dialog.

Native and threaded-browser jobs finalize arrays, transpose histories, hash,
compress and replace files on the worker. Threadless builds advance bounded
encoding and compression steps with a two-millisecond polling budget (individual
steps can exceed the budget); snapshot capture still occurs synchronously.
Worker-start failures fail the save rather than running encoding synchronously.
Allocation, serialization and worker-finalization failures retain the previous
file and allow subsequent writes. Browser persistence occurs after local atomic
replacement: if it fails, the new local file remains available for export while
the previously persisted browser copy remains intact. A retry creates a new save
operation; each operation's terminal state is sticky and its success callback
runs once. Other background string writers still keep the newest queued snapshot.

## Embedded recording dependencies

Client builds compile a pinned minimal FFmpeg/x264 stack through
`scons/recording_dependencies.py`; the source SHA-256 lock is
`scons/recording-versions.json`. Native builds need NASM on x86 targets and
`pkg-config`; Linux builds with `libva` development headers include VAAPI. NVIDIA
headers are pinned with the other sources. Mobile archives use the target compiler,
ABI and SDK, including assembly flags; browser archives use standalone wasm32
SIMD without pthreads. Codec optimization flags remain confined to these archives.

Installed archive hashes, source inputs, recipe, compiler, assembler version, target, SDK and feature
flags form the recording cache identity. A mismatching cache is rebuilt rather
than reused. Release source distributions contain the pinned original archives
under `third_party/recording-sources/`, allowing offline rebuilds. Packages include
codec license notices and configuration. Recording capture has a
`recording.capture` performance scope; simulation and replay checksums must match
with recording enabled and disabled. See [gameplay footage](../features/gameplay-recording.md).

### Music encoding

Runtime music uses Ogg Opus (`.opus`), stereo at 48 kHz, with a 48 kbps VBR
target for the whole stereo stream. Vorbis music is no longer supported. Music
sets retain their directory names and contain `a1.opus`, `a2.opus`, and `a3.opus`.
The three tracks must decode to exactly equal positive frame counts, with one
logical stream each, so mood changes remain aligned. Intro and menu music live
in `data/zik/intro.opus` and `data/zik/menu.opus`.

Use `python3 tools/encode_music.py INPUT OUTPUT.opus` to encode from PCM masters
or convert an existing custom Vorbis track. It uses FFmpeg/libopus with
`-b:a 48k -vbr on -application audio -compression_level 10 -ar 48000 -ac 2`,
resets audio timestamps to a zero-based 48 kHz sample clock, fully decodes the
result for validation, and records the recipe and encoder
version beside the output when `--metadata PATH` is supplied. Convert all three
tracks before installing a custom set. Prefer encoding procedural music directly
from rendered PCM; existing static music may be transcoded once. Encoding tools
are development dependencies, not runtime requirements.

The mixer and gameplay recorder share a 48 kHz PCM rate. Voice packets retain
the existing Speex format and are resampled for playback. Music fade duration is
preserved from the previous 44.1 kHz mixer. Browser builds compile checksum-pinned
Opus, opusfile and Ogg libraries separately for serial and threaded runtimes;
opusfile HTTP support is disabled. Native/mobile builds use their package-managed
opusfile dependencies with libogg retained.

### Buffered music playback

`SoundMixer` is an application-thread facade. Its value-only controls, snapshots
and diagnostics live in `MusicTypes.h`; UI callers do not include decoder or queue
internals. Native playback owns one dedicated producer thread; browser playback
uses a separate Wasm decoder worker. Both run
`Music::Producer`, retaining the existing Opus timeline, loop handling, mood
selection and fixed-point fades. Loading, replacing, seeking and decoder cleanup
happen outside the device callback. Preview screens send typed controls and read
consumed playback snapshots instead of locking SDL or owning live decoders. Preview
session tokens prevent an old screen from controlling or closing a newer preview.

The producer maintains 24 blocks of 1,024 stereo frames (512 ms at 48 kHz), refills
at 20 blocks (427 ms), and cannot exceed 48 blocks. Native output consumes a single-producer,
single-consumer ring without waiting for gameplay or decoder locks. Volume and
mute are applied at consumption. Native voice decoding stays on the application
thread and publishes bounded PCM to separate per-player rings; the music look-ahead
does not add voice latency. The producer requests high scheduling priority, but
failure to obtain it is supported and is not an audio initialization failure.
Set `GLOB2_AUDIO_THREAD_PRIORITY=0` to qualify ordinary-priority production.

Mood requests affect future prepared samples, normally within one second. A
request during an existing fade still waits for that fade to complete; rapid
requests coalesce to the latest mood. Replacement and preview controls use queue
generations to reject obsolete samples. Preview pause retains the queue and partial
block; its clock freezes at consumption and resumes without skipping look-ahead
music. Seek invalidates the old generation even while paused. A real underrun fades
out over five milliseconds and resumes with a fade after refilling; it never loops
a stale block.
No finite queue can cover indefinite OS/browser audio-thread starvation.

Loading and replacement on native playback synchronously wait for the producer to
finish preparation; the audio callback continues consuming the old queue meanwhile.
Only a successful replacement invalidates those samples. Routine mood and preview
controls coalesce and never make the callback wait. Decoder ownership and destruction
stay with the producer; only the consumer advances the queue read cursor.

`SoundMixer::diagnostics()` exposes buffered and consumed frames, underruns,
starvation frames, maximum producer render time, callback time, and observed mood
command latency. Browser diagnostics are available through `Module.glob2Music`.
Do not log from the device callback. Queue diagnostics and dummy audio tests cover
application supply; device-loopback capture and listening are separate evidence.
