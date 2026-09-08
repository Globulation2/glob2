# Mobile developer guide

See [architecture](architecture.md) for how the ports share desktop/browser code,
and [verification status](status.md) for the evidence and remaining work.
Run every command from the mobile worktree root.

## Isolated workspace

The development checkout is `/Users/bradley/glob2-mobile` on
`codex/mobile-platform`. Do not build or run tests in the AI Maxima or browser
checkouts. Native desktop and web builds can run here alongside mobile builds.
Keep SDKs, dependencies, caches, generated projects, test profiles, and artifacts
inside this worktree. Never invoke a desktop install target.

## SDK checks

The read-only checker reports the selected compiler, SDK, output identity, and
compiler fingerprint. It does not change the machine's Xcode selection.

```
python3 mobile/doctor.py android
python3 mobile/doctor.py android arch=armeabi-v7a android_sdk=/path/to/sdk
python3 mobile/doctor.py ios environment=simulator developer_dir=/Applications/Xcode.app/Contents/Developer
```

SDK versions are recorded in `mobile/toolchain.json`. Android uses NDK
28.2.13676358, API 26 minimum, and separate arm64-v8a, armeabi-v7a, and x86_64
outputs. iOS uses Xcode 26.6, deployment 15.0, and separate ARM64 device/simulator
outputs. Full Xcode is required; Apple Command Line Tools lack the iOS SDK.

The task-local Android SDK root is `build/mobile-tools/android-sdk` unless
`android_sdk` or `ANDROID_SDK_ROOT` selects another root. SDK installation and
license acceptance are prerequisites. Explicit bootstrap commands below download
into this worktree without changing the system's Xcode selection.

## Android setup and packaging

Run from this worktree with Python 3, SCons, Git, make, autotools, and pkg-config
installed. vcpkg can download supported CMake/build utilities into its local tools.

```
python3 mobile/setup_ndk.py
python3 mobile/setup_tools.py
python3 mobile/dependencies.py --arch arm64-v8a --release
python3 mobile/android.py configure --arch arm64-v8a --release
```

The tool bootstrap pins Gradle and macOS Java 17 / command-line SDK downloads.
Other hosts need Java 17 selected by `JAVA_HOME` and an Android SDK. The vcpkg
baseline pins dependency recipes and archive checksums. Dependencies compile for
the destination without Homebrew. All downloaded and generated files stay local.

For APK packaging on macOS, install SDK packages after reviewing and accepting
Google's SDK license yourself:

```
export JAVA_HOME="$PWD/build/mobile-tools/jdk-17.0.20.1+1/Contents/Home"
export ANDROID_USER_HOME="$PWD/build/mobile-tools/android-user"
build/mobile-tools/android-sdk/cmdline-tools/19.0/bin/sdkmanager \
  --sdk_root="$PWD/build/mobile-tools/android-sdk" \
  'platforms;android-35' 'build-tools;35.0.0' 'platform-tools'
python3 mobile/android.py build --arch arm64-v8a --release
```

Release APKs are unsigned. To create an optimized developer APK with a key kept
inside this worktree:

```
python3 mobile/android.py sign --arch arm64-v8a --release
python3 mobile/android.py install --arch arm64-v8a --release --serial DEVICE_SERIAL
python3 mobile/android.py launch --arch arm64-v8a --release --serial DEVICE_SERIAL
```

This produces `app-release-development.apk` alongside the unsigned APK. Signing
checks the signature and ZIP alignment, and records both APK digests. Installation
rejects changed or stale signed output. The key uses Android's standard developer
alias/password; it is for developer distribution. Store signing remains separate.

For debug APKs with debugger attachment enabled, omit `--release` from
**both** dependency and application builds, then use:

```
python3 mobile/android.py install --arch arm64-v8a --serial DEVICE_SERIAL
python3 mobile/android.py launch --arch arm64-v8a --serial DEVICE_SERIAL
```

Repeat with `--arch armeabi-v7a` or `--arch x86_64` for older Android devices or
emulators. Each architecture/configuration has independent output. Open the
printed `android-project` path in Android Studio; its pre-build task calls SCons.
Select Java 17 and set the IDE's Gradle user home to this worktree's
`build/mobile-tools/gradle-home`. Debug signing uses a task-local key. Local release
signing belongs in the generated project's `signing.gradle`, outside version control.

Every dependency archive member is checked for architecture. Staging verifies
every shared library's LOAD segment alignment (16 KB for 64-bit, 4 KB for ARMv7),
including SDL and the NDK C++ runtime. Also check completed APK ZIP alignment with
`zipalign -c -P 16 -v 4 APP.apk`.

## Isolated Android emulator

The tested host is macOS ARM64, using emulator 37.1.11 and the API 35 default ARM64
system image revision 2. `mobile/emulator.json` records these versions, and the
runner rejects mismatched SDK packages. Install with the SDK environment above:

```
build/mobile-tools/android-sdk/cmdline-tools/19.0/bin/sdkmanager \
  --sdk_root="$PWD/build/mobile-tools/android-sdk" \
  'emulator' 'system-images;android-35;default;arm64-v8a'
python3 mobile/emulator.py configure
```

In separate terminals, run `python3 mobile/emulator.py adb-server` and
`python3 mobile/emulator.py run` (add `--window` for a visible emulator). The default
configuration uses 2 GB RAM, two virtual CPU cores, software graphics, emulator
port 5580 and a separate ADB server on port 15037. The ADB server avoids USB-device
attachment and discovery of other emulators. AVDs, writable images, discovery
files, keys, and crash-report paths are directed into `build/mobile-tools`.

```
python3 mobile/emulator.py status
python3 mobile/android.py install --release --serial emulator-5580 --adb-port 15037
python3 mobile/android.py launch --release --serial emulator-5580 --adb-port 15037
build/mobile-tools/android-sdk/platform-tools/adb -P 15037 -s emulator-5580 \
  exec-out screencap -p > build/android-screen.png
python3 mobile/emulator.py stop
```

Wait for `status` to print `1` with exit status zero before installing. Stop the
separate ADB server with Ctrl-C in its terminal. On x86-64 hosts select the matching
x86-64 image and APK; that emulator-host recipe has not been run locally.

## iOS setup and packaging

Full Xcode 26.6 with device/simulator SDKs and CMake 3.24+ are required.
Xcode is installed through Apple's installer or App Store; it and Apple's secured
runtime storage are system-managed. Generated projects, dependencies, and simulator
device data remain in the mobile worktree. Keep the global `xcode-select` unchanged.

To install the matching ARM64 runtime and create an isolated simulator:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
mkdir -p build/mobile-tools/ios-runtime build/mobile-tools/ios-simulators
xcodebuild -downloadPlatform iOS -architectureVariant arm64 \
  -exportPath "$PWD/build/mobile-tools/ios-runtime"
xcrun simctl runtime scan-and-mount
xcrun simctl list runtimes
xcrun simctl list devicetypes
xcrun simctl --set "$PWD/build/mobile-tools/ios-simulators" create \
  Glob2-Mobile DEVICE_TYPE_ID RUNTIME_ID
xcrun simctl --set "$PWD/build/mobile-tools/ios-simulators" boot SIMULATOR_UDID
xcrun simctl --set "$PWD/build/mobile-tools/ios-simulators" bootstatus SIMULATOR_UDID -b
```

Use IDs returned by the list/create commands. The install/launch commands below
use this worktree's device set by default; `--simulator-set PATH` selects another
explicit set. Apple verifies and manages the runtime image; the exported bundle
is an optional local copy. Runtime installation may
require substantial disk space. Do not delete other projects or simulator devices.

 Select
Xcode without changing `xcode-select` by passing `--developer-dir`:

```
python3 mobile/dependencies.py --target ios --environment simulator --release \
  --developer-dir /Applications/Xcode.app/Contents/Developer
python3 mobile/ios.py build --environment simulator --release \
  --developer-dir /Applications/Xcode.app/Contents/Developer
python3 mobile/ios.py install --environment simulator --release --device SIMULATOR_UDID \
  --developer-dir /Applications/Xcode.app/Contents/Developer
python3 mobile/ios.py launch --environment simulator --release --device SIMULATOR_UDID \
  --developer-dir /Applications/Xcode.app/Contents/Developer
```

Use `--cmake` to select a CMake executable. Device builds use `--environment device`
for both commands and `--team APPLE_TEAM_ID` for application configuration. Signing
certificates and matching provisioning profiles must already be available locally.
Simulator builds require no credentials. `--unsigned` also permits device compilation
without signing credentials; that output cannot be installed on a device.
The generated Xcode project invokes SCons
for the shared core. Xcode 26.6 simulator builds and unsigned device builds have
passed locally. Physical-device signing and installation remain unverified.
Release builds produce a matching `Glob2.app.dSYM` alongside the application.
The CoreBluetooth framework is linked for the pinned SDL controller support.

## Cross compilation

```
scons target=android arch=arm64-v8a release=1 mobile_deps=/path/to/target/deps
scons target=android arch=armeabi-v7a release=1 mobile_deps=/path/to/target/deps
scons target=ios environment=simulator release=1 mobile_deps=/path/to/target/deps
```

Mobile C++ artifacts use the shared source manifests. Android produces
`lib/libmain.so`; iOS produces `lib/libglob2.a` for the Xcode application link.
Packaging scripts consume these artifacts. A compiled library alone does not
demonstrate a launchable or usable mobile application.

Dependencies must include SDL2 (with SDL_RenderGeometry), SDL2_image, SDL2_ttf,
SDL2_net, Vorbis/Ogg, zlib, Boost headers, and the native transport's current
OpenSSL libraries. Voice recording and the optional IRC bridge are excluded.
System WebSocket transports will remove mobile OpenSSL linking when integrated.

Each dependency prefix has `include/` and a `manifest.json` containing:

- `identity`: the exact identity printed by `mobile/doctor.py`;
- `toolchain`: the printed compiler fingerprint;
- `archives`: relative link-library filenames mapped to SHA-256 digests, in link
  order (including transitive static dependencies).

Every listed library is checked before use. Host include/library environment
injection is removed. Configuration and compiler/dependency changes cannot reuse
incompatible objects. Compiler databases are emitted inside each target output.
The archive manifest verifies library outputs. The pinned vcpkg baseline supplies
source checksums; clean-build reproducibility remains to be qualified on CI hosts.

## Debugging and cleanup

Use each target's `compile_commands.json` with clangd. Android libraries retain
debug information and release packaging requests full native symbols. Attach
Android Studio LLDB to debug builds or use the generated Xcode project. IDE
debugger attachment, sanitizers, and device profiling remain unqualified. Use
Android Studio CPU/memory profiling and Xcode Instruments Time Profiler/Allocations
for eventual performance measurements; host tests establish no device performance.

`scons target=android arch=ARM_ARCH release=1 mobile_deps=PREFIX -c` cleans that
native target. Clean packaging through Gradle `clean` or Xcode Clean Build Folder.
Dependency trees are target-local `vcpkg-*` directories; SDKs/downloads are shared
only within `build/mobile-tools`. Never run a desktop install target for this work.

Android assets extract from the package into a versioned private root; iOS reads
bundled resources. Writable data uses SDL's application preference path. Extraction
is currently synchronous at startup. Transactional saves, recovery generations,
import/export, and bounded asset-loading UI remain outstanding.

## Shared regression checks

```sh
scons release=1 -j8 portable-renderer-test mobile-input-test screen-test \
  responsive-menu-test gameplay-touch-test session-test portable-game-test
./build/darwin/client/release/libgag/src/PortableRendererHarness
./build/darwin/client/release/libgag/src/MobileInputHarness
./build/darwin/client/release/libgag/src/ScreenExecutionHarness
mkdir -p build/mobile-test-profile
GLOB2_USER_DATA_DIR="$PWD/build/mobile-test-profile" \
  ./build/darwin/client/release/src/responsive-menu-test
GLOB2_USER_DATA_DIR="$PWD/build/mobile-test-profile" \
  ./build/darwin/client/release/src/gameplay-touch-test
GLOB2_USER_DATA_DIR="$PWD/build/mobile-test-profile" \
  ./build/darwin/client/release/src/portable-game-test
python3 test/run-engine-session-test.py
python3 -m unittest discover -s tests/build_system -v
```

Use `build/linux/` on Linux. Renderer tests need a graphical session (Xvfb on
Linux). Build the standalone tests in `test/` before using
`python3 test/run-standalone-tests.py`; its profiles stay under `build/test-profiles`.
These checks do not qualify real-device performance or cross-architecture determinism.

Follow the [browser build guide](../browser/implementation.md) for the locked Emscripten
setup. After a web build, run the focused shared-host regressions:

```sh
mkdir -p build/browser-test-tmp
TMPDIR="$PWD/build/browser-test-tmp" \
  PLAYWRIGHT_BROWSERS_PATH="$PWD/build/mobile-tools/playwright" \
  browser/node_modules/.bin/playwright test -c browser/playwright.config.js \
  --grep 'visibility callbacks|a running match survives resize|tutorial sessions quit' \
  --output=build/mobile-browser-results
```

## Browser synchronization

Check the latest browser branch before a milestone. The tested base is recorded
in [status](status.md). Inspect the remote branch without changing another checkout:

```sh
git fetch origin codex/browser-experiment
git merge-base --is-ancestor origin/codex/browser-experiment HEAD
```

If ancestry fails, inspect the divergence before integrating into the mobile
branch. A rewritten browser branch may require replaying mobile commits rather
than merging duplicate simulation changes. Preserve work first. After integration,
rebuild native, Android, and web clients and run lifecycle, session, viewport,
and browser persistence regressions. Keep the Emscripten lock unchanged unless
an intentional update passes desktop/browser gates.

## iOS diagnostics and save extraction

Use the exact device set and UDID from setup:

```sh
xcrun simctl --set "$PWD/build/mobile-tools/ios-simulators" io SIMULATOR_UDID \
  screenshot "$PWD/build/ios-screen.png"
xcrun simctl --set "$PWD/build/mobile-tools/ios-simulators" get_app_container \
  SIMULATOR_UDID org.globulation.glob2 data
xcrun simctl --set "$PWD/build/mobile-tools/ios-simulators" shutdown SIMULATOR_UDID
```

The returned app-data container contains SDL's writable preference path, including
created saves/maps/replays. Copy files from this container for inspection; leave
bundle assets untouched. LLDB can use the target compilation database and dSYM;
verify executable/symbol UUIDs with `xcrun dwarfdump --uuid APP/Glob2 APP.dSYM`.
An unsigned device application is a compilation artifact and cannot be installed.
Do not interpret simulator smoke tests as iOS 15 hardware, touch-layout, thermal,
or performance qualification.
