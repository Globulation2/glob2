# Mobile toolchains and assets

Pinned dependencies, isolated build outputs, icons and recording libraries.

## Toolchains and output isolation

Run commands from the repository root. Versions and checksums are pinned in
`mobile/toolchain.json`, `mobile/android-tools.json` and `mobile/vcpkg.json`.

Android CI opts into the shared `CCACHE=1` compiler wrapper and restores a
per-architecture cache keyed by the pinned toolchain, dependency manifest and
triplets. A cache miss still builds normally; ccache checks compiler content and
source dependencies before reusing an object. Leave `CCACHE` unset when generating
`compile_commands.json` for source analysis. A newer PR push cancels its older
Android checks. The APK workflow does not build the native doctest harnesses,
so changes confined to those harnesses run native CI without rebuilding all three
Android APKs. Changes to the shared CI summary script still select the APK jobs.

Use `python3 mobile/doctor.py android` or `python3 mobile/doctor.py ios` to inspect
SDK availability. iOS requires the pinned full Xcode 27.0 toolchain;
`--developer-dir` on the packaging command selects it without changing the
machine's Xcode selection.

Build outputs live under
`build/<android|ios>/<device|simulator>/<arch>/<api>/<client>/<debug|release>`.
Dependency manifests bind archive hashes to the target and compiler. SDKs,
emulators, generated projects, signing keys and caches stay under `build/`.

### Text raster resolution

Screen text uses the drawable pixel scale multiplied by the active UI transform,
including the SDL portable renderer on Android/iOS. Font layout measurements stay
at their authored size; only glyph rasterization changes. Offscreen surfaces keep
logical-resolution text because they cannot retain extra screen pixels. Raster
fonts and string caches are keyed by integer font size so alternating HUD/control
scales do not reopen fonts or discard other labels each frame. Verify sharpness
on device screenshots: a density-1 emulator can hide an enlarged-glyph defect.
`GameGUITouchHarness` checks first-draw resolution, stable metrics, offscreen text
and cache reuse under alternating transforms.


## App icons

Both mobile packages use the canonical blue Glob artwork from
`data/icons/glob2-icon-128x128.png`, on the in-game purple background. Android
ships density-specific legacy icons and a padded adaptive foreground; iOS ships
an opaque iPhone/iPad AppIcon asset catalog, compiled by Xcode into the bundle.
The canonical source is 128 pixels; larger sizes are resampled from that source.

The generated assets are checked in, so normal builds need no image tooling.
After changing the canonical source, run `python3 mobile/icons.py` with Pillow
installed and review both the adaptive foreground and opaque icon before committing.


## Packaged image assets

Release Android and iOS packages use the shared runtime asset exporter described
in [the development reference](../development/package-size.md#release-asset-and-bundle-sizes).
It generates verified WebP/PNG assets without editing source artwork. Debug
packages retain PNGs; both profiles retain existing image-directory override
precedence. Mobile SDL_image dependencies explicitly enable WebP, so rebuild the
pinned dependency bundle after this manifest changes. Android indexes and hashes
the exported payload; retain APK/AAB verification after AAPT packaging and after
installing an update, which must not keep an obsolete PNG in front of a new WebP.

Candidate validation uses `python3 mobile/android_release.py check-candidate`; this checks identity and build recipes without requiring a new publication tag. `check` retains strict tag-collision and version-code checks for release publication.


## Recording libraries and exports

Mobile SCons builds compile the pinned minimal FFmpeg/x264 recording libraries
for each ABI and SDK; Xcode links the archives and VideoToolbox, while Android
links MediaCodec through the NDK. Compiler, SDK, flags and source hashes invalidate
the dependency cache. App packages include recording dependency notices.

Recordings stay in app-private storage. The Recordings screen hands a file path
to the native document picker and streams Android exports, avoiding a full video
copy in game memory. Execution suspension preserves timestamp gaps; background
execution and hardware/thermal qualification require physical device testing.
See [gameplay footage](../features/gameplay-recording.md).

For focused custom-generator review, install the example packages in a disposable
profile and set `GLOB2_GALLERY_GENERATORS_ONLY=1` when running `mobile-gallery`.
This captures the package list, the scripted editor selection and a generated
local-game preview using `examples:swamp`.


## Fast native development compilation

After provisioning the pinned SDK and mobile dependencies, use
`python3 tools/dev_build.py target=android arch=arm64-v8a` or
`python3 tools/dev_build.py target=ios environment=simulator`. These build the
existing native artifacts in separate fast-development directories; packaging,
signing and device installation remain the responsibility of the existing mobile
workflows. Fast/PCH/unity profiles reuse compatible mobile dependency manifests
and never fall back to host libraries. Recording libraries share verified entries
by architecture, SDK, toolchain and flags. Use ordinary debug builds for full
variable inspection and release builds for packaging and performance assessment.
See [fast development builds](../development/build-options.md#fast-development-builds)
for profile flags, dependency parallelism, cache controls and verification.

[Mobile index](README.md) · [Documentation index](../README.md).
