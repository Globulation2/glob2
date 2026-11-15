# Vendored dependencies

## JSON for Modern C++

nlohmann/json v3.12.0 (MIT), from
https://github.com/nlohmann/json/releases/tag/v3.12.0 (tag commit
`65ee68451d8eb2b5f3a30b410476ab83deb3289b`). `nlohmann-json/include/nlohmann/`
holds the release's unmodified single-header amalgamation, `json.hpp`
(SHA256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`,
identical to the release asset and to `single_include/` at the tag), and
`json_fwd.hpp` from `single_include/` (SHA256
`fb6aa70cbece087f37ab4685c182b287c53be54f785f981b9db9d30d2d028b37`), with the
upstream `LICENSE.MIT`. Include it as `<nlohmann/json.hpp>`: native, browser and
mobile builds share `third_party/nlohmann-json/include` through
`INCLUDE_DIRECTORIES` in `buildsys/sources.py`. It is for platform JSON (realtime
messages, REST bodies, match setup), never for simulation state, saves or
replays. To update, replace both headers and the licence from a new release and
record the new tag, commit and hashes here.

## Texture block compression

etcpak 2.1 (BSD 3-Clause), from https://github.com/wolfpld/etcpak (tag `2.1`,
commit `7f15da4739cc5f8562311bdbb800436986d268ca`). `etcpak/etcpak/` holds only
the block encoders and what they include: `ProcessDxtc`, `ProcessRGB`, `Tables`
and `Dither` (`.cpp` and `.hpp`), `ForceInline.hpp`, `Math.hpp`, `Vector.hpp`
and `ProcessCommon.hpp`, with the upstream `LICENSE.txt` and `AUTHORS.txt`
beside it. The command-line tool, image loading, decoders, threading and the
BC7 encoder are left out. The sources are unmodified except
`patches/etcpak-0001-no-bc7.patch`, which removes `CompressBc7` and its
`bc7enc.h` include from `ProcessDxtc`, so `bc7enc` need not be vendored.

`buildsys/sources.py` compiles the four sources into libgag on every platform
(`ETCPAK_SOURCES`) and puts `third_party/etcpak` on the include path; include
the encoders as `<etcpak/ProcessDxtc.hpp>` and `<etcpak/ProcessRGB.hpp>`.
`AssetImage::compressBlocks` (`libs/gag/src/AssetLoader.cpp`) is the only
caller: asset workers encode high-resolution texture levels to BC3, or to ETC2
with EAC alpha for GPUs without S3TC. The encoders pick SSE4.1, AVX2 or NEON
paths from the compiler's target macros, so the baseline x86-64 build of these
four sources is the portable path. `libs/gag/src/EtcpakSse41.cpp` compiles them
a second time inside a namespace, with `-msse4.1` for that file alone on x86
GCC and Clang builds (`CMakeLists.txt`), and the encoders use that copy when
`__builtin_cpu_supports("sse4.1")` says the processor runs it: about three
times faster, and identical blocks on the cases the `ImageAssets` suite checks.
NEON builds (ARM) take the fast path directly. The notice ships as `content/data/etcpak-license.txt`.
To update, replace the listed files from a new tag, reapply the patch, record
the tag and commit here, and rerun the `ImageAssets` and `GpuTexturePreparation`
suites, which decode both formats.

## ASTC texture compression

astcenc 5.7.1, the Arm ASTC Encoder (Apache License 2.0), from
https://github.com/ARM-software/astc-encoder (tag `5.7.1`, commit
`0affb51aaf0e0de3a8b038fa32487d59754a1eed`). Added as a runtime dependency with
maintainer approval, for SDL GPU devices that sample ASTC but not BC3 (phones).
`astcenc/astcenc/` holds only the core codec library from upstream `Source/`:
the 22 sources `Source/cmake_core.cmake` builds into its static library and the
headers they include (`astcenc.h`, `astcenc_internal.h`,
`astcenc_internal_entry.h`, `astcenc_mathlib.h`, `astcenc_diagnostic_trace.h`
and the `astcenc_vecmathlib*.h` SIMD back ends), with the upstream `LICENSE.txt`
beside it. Upstream has no NOTICE file. The command-line tool, its image
loaders and `Source/ThirdParty/`, the tests, fuzzers and documentation are left
out. The sources are unmodified.

`buildsys/sources.py` compiles them into libgag on native and mobile platforms
(`ASTCENC_SOURCES`) and puts `third_party/astcenc` on the include path; include
the API as `<astcenc/astcenc.h>`. The browser build leaves them out with
`AstcEncoder.cpp` (it renders through WebGL 2, never SDL GPU).
`libs/gag/src/AstcEncoder.cpp` is the only caller: asset workers encode premultiplied high-resolution levels
(`AssetImage::BlockCompression::PremultipliedASTC4x4`) with a trimmed fastest
preset into 4x4 LDR blocks, each worker thread through its own child context sharing
one root context's tables. astcenc picks its SIMD back end from the compiler's
target macros: NEON on arm64 (Android `arm64-v8a`, iOS), SSE4.2 and POPCNT on
Android `x86_64` (that ABI's baseline), SSE2 on desktop x86-64, and the scalar
path on 32-bit ARM. Unlike etcpak there is no second, run-time-selected
SSE4.1 build: desktops sample BC3, so the SSE2 build is only their fallback,
and a copy would add about 275 KB for about 10% faster encoding. The notice ships as `content/data/astcenc-license.txt`. To update, replace the
listed files from a new tag, record the tag and commit here, and rerun the `ImageAssets`, `RenderDevice` and
`SDLGPUDevice` suites, which encode, decode and sample the blocks.

## Scripting runtime


QuickJS-NG v0.17.0 (MIT), from https://github.com/quickjs-ng/quickjs/releases/tag/v0.17.0.
OpenLibm v0.8.8, from https://github.com/JuliaMath/openlibm/releases/tag/v0.8.8.
Retained license notices accompany each dependency. Only engine sources and the
portable double-precision math subset are retained; command-line and OS bindings
are excluded.

Local changes namespace OpenLibm public math symbols, redirect QuickJS numeric
operations to that subset, meter interpreter dispatch, and reject dynamic
Function compilation and regular-expression literals. Build both libraries with
strict floating-point flags. Upstream updates require rerunning the scripting
capability/resource, numeric, continuation and cross-platform suites.

OpenLibm compatibility aliases are removed so no system math symbol is exported.
QuickJS also uses a fixed context hash seed, a deterministic 256-frame recursion
limit, and lexical rejection of BigInt and async/await. Native operations charge
linearly for receiver/argument container sizes before execution. Profile restrictions
are host/runtime policy. The map-generator host explicitly enables package-local
static imports; dynamic imports, async/await and imports in existing AI/scenario
hosts remain disabled. Never link quickjs-libc or expose the CLI's OS modules.
The host installs only base, JSON and Map/Set intrinsics (Promise/Eval internals
support module evaluation but their user globals and constructors are disabled).

JSON numeric parsing uses QuickJS's own dtoa parser instead of host strtod.
Native allocation and physical-stack failures set a sticky runtime flag checked
by the host even when a script catches the JavaScript exception.

Additional native metering covers actual array-like lengths, JSON tokens and
recursive serialization, recursive flattening, and generated string repeat/pad
lengths. These hooks prevent shared graphs, accessor-provided lengths and
generated output from bypassing the interpreter work budget.

String concatenation, comparisons and dynamic property-key conversion charge
logical string lengths, including rope strings. Tests retain adversarial cases
for these operations as well as exponentially shared JSON/array graphs.

Logical 64-call guards cover recursive parser entry points, JSON traversal and
array flattening; physical stack checks remain a fatal fallback. Container/string gateway
charges are linear. Actual coerced string search lengths, default sort string
comparisons and emitted string-buffer characters are metered within operations.

String/number coercion charges actual string lengths after object conversion.
Native search, sorting and output charges remain in the operations themselves.

`javascript-vendor.json` records the exact upstream commits, tag-archive SHA256s,
retained files and ordered patch series. The first patch for each dependency
reproduces the original profile fork; later patches record the engineering pass
and host-only module-binding access for automatic global persistence. No heap
pointers or executable bytecode are serialized.
Run `python3 tools/javascript/verify-vendor.py` to download the pinned archives,
verify their hashes, reconstruct retained sources in temporary storage and reject
unexplained differences. `--archives /path/to/archives` uses previously downloaded
`quickjs-ng.tar.gz` and `openlibm.tar.gz`. The command never edits the checkout.

OpenLibm `hypot` is retained and namespaced alongside the other double routines.
Half-number conversion and clamped integer rounding also avoid system math.
The build checks interpreter, math and host conversion objects for unexpected
platform numeric symbols. The operation inventory and trusted-code assumptions
are maintained in [the scripting guide](../docs/scripting/javascript.md).
