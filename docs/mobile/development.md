# Mobile build foundations

Android and iOS use the shared game sources and SDL renderer. The mobile targets
have isolated toolchains, dependency archives and output directories; they do not
use host libraries or install into the desktop application's directories.

The shared menus, game UI, simulation and screen lifecycle follow the desktop and
browser implementation. Phone-specific layouts, touch gameplay gestures,
safe-area integration and background/foreground recovery still need integration
with that implementation. The packaging and smoke tools do not establish that
these interactions are ready for release. Voice capture is disabled on mobile.

## Toolchains and output isolation

Run commands from the repository root. Versions and checksums are pinned in
`mobile/toolchain.json`, `mobile/android-tools.json` and `mobile/vcpkg.json`.
Use `python3 mobile/doctor.py android` or `python3 mobile/doctor.py ios` to inspect
SDK availability. iOS requires full Xcode; `--developer-dir` on the packaging
command selects it without changing the machine's Xcode selection.

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
scons release=1 portable-renderer-test mobile-input-test
build/darwin/client/release/libgag/src/MobileInputHarness
build/darwin/client/release/libgag/src/PortableRendererHarness
```

Substitute the host toolchain directory on Linux or Windows. The renderer harness
needs an accelerated SDL display. These checks cover build isolation, artifact
validation, rendering and input geometry; they do not replace device gameplay,
background recovery, document-picker or cross-platform simulation checks.

`mobile/smoke.py`, `mobile/ios_smoke.py` and `mobile/android_trust_test.py` retain
platform diagnostics under `artifacts/`. Their legacy UI sequences may require
updating as the shared interface evolves. Native document picker and certificate
trust adapters are selected only for mobile builds. Simulation, replay and save
compatibility must be checked with the shared replay-verification workflow before
shipping a mobile client.
