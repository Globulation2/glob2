# Runtime assets and package size

How the shared asset pipeline prepares runtime data and how to measure package changes.

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
checksum-pinned PNG/JPEG/WebP SDL3_image. Its cache identity includes compilers,
CMake, codec options, dependency versions and selected library hashes. Linux
installs its canonical decoder under `lib/glob2` with soname symlinks and an
executable-relative search path. Windows staging automatically prioritizes the
private runtime recorded beside its build binary. These builds use a separate
`lean-images` build directory; ordinary builds and RPM retain system libraries.
Snap stages the required JPEG/PNG/WebP runtimes; Flatpak builds the same codec
allowlist directly. PNG/JPEG saving remains enabled for screenshots and maps.

Mac `bundle`/`package` additionally builds a checksum-pinned SDL3_image 3.4.6
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
