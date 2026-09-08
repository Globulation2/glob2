# Mobile verification and remaining work

Recorded 2026-09-08. Browser base: `5397fbd415786e005115c4c5a23d4767d7d650e3`,
also the remote `codex/browser-experiment` tip when checked. Runtime implementation:
`7333e4e8b`. Subsequent documentation, CI packaging, and iOS argument-preflight
changes do not change game simulation or native rendering.

## Current evidence

| Area | Evidence | Limit |
| --- | --- | --- |
| Desktop and browser | Native macOS release and Wasm release builds pass | Other desktop platforms not rebuilt locally |
| Android ARM64 | Release APK packaged, alignment checked, developer signed, installed and launched on API 35 ARM64 emulator | No physical device qualification |
| Android ARMv7 / x86-64 | Release APK builds, ELF/package alignment checks, and developer signing pass | No launch evidence on these ABIs |
| iOS device / simulator | SDK discovery, separate identities, dependency and Xcode generation recipes implemented | Full Xcode is absent; neither app variant has been compiled or launched |
| Shared lifecycle | Native screen/session/renderer harnesses pass; Android retained-activity background/resume exercised | Killed-process and multiplayer recovery unfinished |
| Native touch and viewport | Responsive-menu and gameplay-touch integration harnesses pass; Android tutorial selection, placement, confirmation, rotation cancellation, and pan exercised | Complete phone panels and editor touch parity unfinished |
| Browser compatibility | Nine tutorial/visibility/resize cases pass across Chromium, Firefox, WebKit after the final viewport changes | Not mobile Safari/Chrome device qualification |
| Build tooling | 20 Python checks pass, including invalid identities, archive alignment, signed APK provenance, and fail-fast iOS requests | Does not prove iOS linking or IDE debugging |
| CI | Android ABI matrix now packages, checks alignment, signs developer APKs, and collects symbols/diagnostics | Updated workflow has not yet been run; no automated emulator or iOS simulator gate |

The three native 50-tick engine-session variants agree on `7e7f31de` after
background and child-screen interruption. This is a lifecycle regression result,
not the requested 100,000-tick ARM/Wasm determinism qualification.

Browser synchronization also passed 24 viewport/storage cases and three visibility
cases on the current browser base. One Firefox aborted-save retry timed out while
Android compilation ran concurrently; its isolated rerun passed with unchanged
assertions. Older pre-sync test counts and AI failures do not qualify this base.

## Android emulator screenshots

Captured on the API 35 ARM64 emulator on 2026-09-08, using runtime commit
`7333e4e8b`. These are existing captures from the touch/viewport smoke test.
Dark world regions are unexplored terrain; legacy panels and tutorial text still
need phone reflow. These screenshots establish functional rendering, not device
performance or iOS support.

Portrait placement preview with the explicit Confirm/Cancel strip:

![Android portrait placement preview](screenshots/android-preview.png)

Portrait after confirming construction through the shared game order:

![Android portrait confirmed construction](screenshots/android-confirmed.png)

Landscape after rotation cleared the placement gesture (confirmation disabled):

![Android landscape after rotation](screenshots/android-landscape.png)

## Reproduction artifacts

The [developer guide](development.md) contains build and regression commands.
Local ignored artifacts in the mobile worktree include:

- `build-touch-fullscreen-tests.log`: native renderer, screen, menu, touch, session checks.
- `build-touch-fullscreen-browser-tests.log`: nine browser checks.
- `build/touch-fullscreen-*.png`: Android portrait/landscape, selection, placement and pan captures.
- `build-foundation-tool-tests.log`: 20 build-tool checks.
- `build-foundation-ios-doctor.log`: missing full-Xcode diagnostic.
- `build-foundation-armv7.log` and `build-foundation-x86_64.log`: current ABI packaging runs.

Release developer APKs live under
`build/android/device/<abi>/26/client/release/android-project/app/build/outputs/apk/release/`.
Signing keys remain local and are not CI artifacts. CI uses runner-provided SDK
packages and Java 17 alongside the pinned NDK and Gradle; a fully archive-pinned
Linux JDK/SDK bootstrap remains to be completed.

The local iOS diagnostic reports that the selected developer directory is
`/Library/Developer/CommandLineTools`, which lacks Xcode's iOS SDK. Install the
pinned full Xcode version and pass `--developer-dir`; simulator/unsigned
compilation needs no signing credentials. Device installation additionally needs
local signing identity and provisioning configuration.

## Next foundation gates

1. Compile and launch the ARM64 iOS simulator application with full Xcode; then
   verify unsigned device compilation and locally signed device installation.
2. Run Android CI packaging and launch checks on the remaining ABIs; add automated
   emulator/simulator startup, lifecycle, and asset-path checks with diagnostics.
3. Finish incremental modal/lifecycle coverage and durable native storage before
   relying on background or process recovery. Qualify graphics restoration on devices.
4. Verify IDE native debugging, release symbols, sanitizer support, and profiling;
   complete host toolchain pinning and reproducible clean-build evidence.

Pinch zoom is explicitly deferred. Further phone panel/editor reflow, browser touch
integration, native system WebSockets, coordinated network recovery, transactional
saves, import/export, and full mobile-browser support remain planned work.
Do not claim the full six-milestone mobile delivery is complete.

## Device qualification still required

The target is iOS/iPadOS 15+ ARM64 and Android 8+ ARM64/ARMv7, including 2 GB devices.
Required hardware includes a first-generation iPhone SE or equivalent, a 2 GB
Android 8 ARMv7 device, modern Android ARM64, and iPad. Emulation is insufficient.

The fixed 128×128 four-player late-game fixture must sustain 25 simulation ticks/s
and at least 30 rendered frames/s for 30 minutes below 500 MiB peak process memory.
Newer hardware targets 60 FPS. Cross-architecture 100,000-tick checks, mixed-platform
multiplayer, thermal/audio interruption, browser eviction, and storage fault tests
remain unqualified. Larger maps must load or fail clearly without changing rules.

The emulator displayed a System UI nonresponse dialog during concurrent compilation
and recovered with Wait. No device performance conclusion follows from that run.
Voice chat, cloud saves, store submission, and new visual identity remain deferred.
