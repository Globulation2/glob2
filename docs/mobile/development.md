# Mobile builds and interface

Android and iOS use the shared game sources and SDL renderer. The mobile targets
have isolated toolchains, dependency archives and output directories; they do not
use host libraries or install into the desktop application's directories.

The phone presentation shares simulation, game orders, settings persistence and
lobby setup with desktop. `InterfacePresentation.h` selects the presentation;
`GameGUITouch` owns gameplay gestures and phone panels, while `PhoneForm` adapts
legacy widget forms. Composed settings and lobby screens supply their own phone
layouts. Automatic presentation uses available logical space and touch capability
on every host. Settings offers Automatic, Compact and Spacious. Spacious requires
480 points of map width beside the panel and 480 points of usable height;
the panel is 288 points for touch and the existing 160 points for mouse controls.
Spacious falls back to Compact when it cannot fit. Safe areas and interface scale
participate in fit; the onscreen keyboard only reduces dialog space.
Touch-capable hosts use targets at least 48 points tall. Pointer availability
controls hover hints without switching layouts. Resize preserves the camera,
selection, tool, panel, scroll position and text while cancelling held input.

`GLOB2_MOBILE_UI=1` forces the compact presentation for development; `0` forces
legacy controls. It takes precedence over the legacy `GLOB2_PHONE_FORMS`,
`GLOB2_RESPONSIVE_UI` and `GLOB2_TOUCH_HUD` opt-ins. Rendering backend and mouse
motion do not select a presentation. SDL, OpenGL/WebGL and software rendering
share UI transforms and clipping. Mouse clicks use the same controls as touch;
Tab and Enter navigate adapted forms, and Page Up/Down scroll gameplay panels.
Mobile settings omit desktop window sizes, renderer selection and OpenGL-only
options because the operating system manages the viewport and the mobile build
uses the portable renderer.

The gameplay toolbar opens build choices, flags/zones, inspection, objectives,
alliances and the game menu. Drag the map to pan, pinch to zoom, tap to select.
Building placement has a movable preview followed by explicit confirmation.
Scrollable drawers keep secondary controls out of the map; rotation, loss of
focus and child screens cancel held gestures. Resize retains the placement preview
and requires a new confirmation gesture. ScreenStack restores each
screen's viewport policy and suspends rendering during SDL background events.

Shared actions belong in GameGUI or the existing setup/settings model, not in
phone coordinate emulation. Construction, destruction, worker allocation,
priority and flag range use shared action methods. Some information/editor
panels still adapt legacy controls; these are candidates for further phone
layout work. Voice capture is disabled on mobile. Native phone-size checks do
not establish Android/iOS release readiness; device gameplay and lifecycle
validation remain necessary.

## Toolchains and output isolation

Run commands from the repository root. Versions and checksums are pinned in
`mobile/toolchain.json`, `mobile/android-tools.json` and `mobile/vcpkg.json`.
Use `python3 mobile/doctor.py android` or `python3 mobile/doctor.py ios` to inspect
SDK availability. iOS requires the pinned full Xcode 27.0 toolchain;
`--developer-dir` on the packaging command selects it without changing the
machine's Xcode selection.

Build outputs live under
`build/<android|ios>/<device|simulator>/<arch>/<api>/<client>/<debug|release>`.
Dependency manifests bind archive hashes to the target and compiler. SDKs,
emulators, generated projects, signing keys and caches stay under `build/`.

## Android

Install Python, SCons, Git, make, autotools and pkg-config. Bootstrap the pinned
NDK and packaging tools, then build target dependencies and stage the project:

```sh
python3 mobile/setup_ndk.py
python3 mobile/setup_tools.py
python3 mobile/dependencies.py --arch arm64-v8a --release
python3 mobile/android.py configure --arch arm64-v8a --release
```

Install the Android SDK platform and build-tools versions listed in the toolchain
manifest using `sdkmanager`, accepting the SDK license yourself. Then package:

```sh
python3 mobile/android.py build --arch arm64-v8a --release
python3 mobile/android.py sign --arch arm64-v8a --release
python3 mobile/android.py install --arch arm64-v8a --release --serial DEVICE_SERIAL
python3 mobile/android.py launch --arch arm64-v8a --release --serial DEVICE_SERIAL
```

Release builds start unsigned. `sign` uses a local developer key and validates
ZIP alignment, the signature and APK digest. Store distribution requires separate
signing arrangements. Use `armeabi-v7a` or `x86_64` for other supported targets.
Omit `--release` consistently from both dependency and application commands for
debug builds. An explicit `JAVA_HOME` takes precedence over the task-local JDK.

## iOS

Build dependencies and the generated Xcode project with the same configuration:

```sh
python3 mobile/dependencies.py --target ios --environment simulator --release
python3 mobile/ios.py build --environment simulator --release
python3 mobile/ios.py install --environment simulator --release --device SIMULATOR_UUID
python3 mobile/ios.py launch --environment simulator --release --device SIMULATOR_UUID
```

The simulator tools use an isolated device set under
`build/mobile-tools/ios-simulators`. Device builds use `--environment device` and
require either `--team TEAM_ID` with local provisioning or `--unsigned` for a
compile-only build. An unsigned device app cannot be installed. Release symbols
are retained alongside the application.

## Verification

```sh
python3 -m unittest discover -s tests/build_system -v
scons release=1 portable-renderer-test mobile-input-test gameplay-touch-test responsive-menu-test mobile-presentation-test
build/darwin/client/release/libgag/src/MobileInputHarness
build/darwin/client/release/libgag/src/PortableRendererHarness
mkdir -p artifacts/mobile-ui/gameplay artifacts/mobile-ui/menu artifacts/mobile-ui/presentation
GLOB2_USER_DATA_DIR="$PWD/artifacts/mobile-ui/gameplay" build/darwin/client/release/src/gameplay-touch-test
GLOB2_USER_DATA_DIR="$PWD/artifacts/mobile-ui/menu" build/darwin/client/release/src/responsive-menu-test
GLOB2_USER_DATA_DIR="$PWD/artifacts/mobile-ui/presentation" build/darwin/client/release/src/mobile-presentation-test
```

Substitute the host toolchain directory on Linux or Windows. The renderer harness
needs an accelerated SDL display. These checks cover build isolation, artifact
validation, rendering and input geometry; they do not replace device gameplay,
background recovery, document-picker or cross-platform simulation checks.

`mobile/ios_smoke.py` checks startup, background/resume and fresh-process relaunch
on a booted simulator named `Glob2-Mobile` in the isolated device set. Pass its
UUID with `--device`; diagnostics default to `build/mobile-smoke-ios`.
`mobile/smoke.py` and `mobile/android_trust_test.py` provide Android diagnostics;
their UI sequences may require updating as the shared interface evolves.
Keep screenshots and logs under ignored `build/` or `artifacts/` directories.
Native document picker and certificate
trust adapters are selected only for mobile builds. Simulation, replay and save
compatibility must be checked with the shared replay-verification workflow before
shipping a mobile client.
