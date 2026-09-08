# Mobile development

All mobile work lives in `codex/mobile-platform`, based on browser commit
`de12b855c899cf1f50ca308cc6b777cb139fd5f4`. Use the isolated `glob2-mobile`
worktree. Do not build, install, or run tests in the AI Maxima or browser worktrees.
Keep test profiles under `build/`; game launches must not write the normal profile.

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
outputs. iOS uses Xcode 26.2, deployment 15.0, and separate ARM64 device/simulator
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

Full Xcode 26.2 with device/simulator SDKs and CMake 3.24+ are required. Select
Xcode without changing `xcode-select` by passing `--developer-dir`:

```
python3 mobile/dependencies.py --target ios --environment simulator --release \
  --developer-dir /Applications/Xcode.app/Contents/Developer
python3 mobile/ios.py build --environment simulator --release \
  --developer-dir /Applications/Xcode.app/Contents/Developer
python3 mobile/ios.py install --environment simulator --release --device SIMULATOR_UDID
python3 mobile/ios.py launch --environment simulator --release --device SIMULATOR_UDID
```

Use `--cmake` to select a CMake executable. Device builds use `--environment device`
for both commands and `--team APPLE_TEAM_ID` for application configuration. Signing
certificates and matching provisioning profiles must already be available locally.
Simulator builds require no credentials. `--unsigned` also permits device compilation
without signing credentials; that output cannot be installed on a device.
The generated Xcode project invokes SCons
for the shared core. These recipes are unverified until full Xcode is available.

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

## Portable renderer

```
scons release=1 -j8 portable-renderer-test
./build/darwin/client/release/libgag/src/PortableRendererHarness
```

The SDL triangle renderer is opt-in on desktop using `GLOB2_RENDERER=sdl` and
selected by mobile compilation. Existing desktop GL/software defaults remain.
The harness tests real presentation resources, clipping, alpha, scaled textures,
texture invalidation, and dirty-surface reupload. Run it in a graphical session;
Linux CI uses Xvfb. Backend performance and complete game visual parity are not
certified by this primitive-level harness.

```
scons release=1 -j8 mobile-input-test portable-game-test screen-test session-test
./build/darwin/client/release/libgag/src/MobileInputHarness
./build/darwin/client/release/libgag/src/ScreenExecutionHarness
mkdir -p build/scene-profile
GLOB2_USER_DATA_DIR="$PWD/build/scene-profile" ./build/darwin/client/release/src/portable-game-test
python3 test/run-engine-session-test.py
python3 test/run-standalone-tests.py
python3 -m unittest discover -s tests/build_system -v
python3 browser/setup.py
python3 browser/build.py
```

Use `build/linux/...` on Linux. The scene harness captures a complete game in its
isolated profile. Screenshot capture precedes presentation to preserve backbuffer
contents. The touch harness checks gesture ownership and coordinates; it does not
prove that gameplay or editor controls are wired to those actions.

Build the standalone regressions with SCons in `test/` before running their script.
They now request C++20, matching the browser branch's coroutine headers. The runner
keeps profiles and temporary campaign fixtures under `build/test-profiles`.

Known baseline failure: `TestsRunner` runs 194 cases with three Maxima placement
failures (`testColonyPurposeDistanceCornAndBlockedIndependence`,
`testColonyThreatAndConqueredScoring`, `testDisconnectedColonyRequiresSwimmingBuilders`).
All three reproduced on a snapshot of base commit `de12b855`, with only the test
compiler mode updated to C++20 so the coroutine headers compile. The other 19
standalone regression binaries pass. Mobile work does not change AI logic to make
these tests pass; this remains a red baseline gate requiring separate diagnosis.

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

## Responsive menus

The native mobile main menu and editor entry menu opt into a logical viewport
sized to the window, using Android's configured UI density. Buttons use the
existing artwork with at least 48 logical units of hit height. The layout chooses
one or two columns from available width and translated label widths; long labels
wrap. Drag or use the mouse wheel to scroll, with a visible scroll indicator.
Touch selection waits for release and cancels after an 8-unit drag, focus loss,
rotation, or a child transition. Synthesized touch mouse events are suppressed
inside these adapted menus. Hardware mouse and keyboard actions remain available.

Desktop developers can exercise this path using `GLOB2_RENDERER=sdl` and
`GLOB2_RESPONSIVE_UI=1`. Default desktop and browser rendering retain their current
layouts. Other screens restore their fixed logical viewport; their responsive
conversion, mobile-browser activation, iOS safe-area/keyboard metrics, and a
user-facing UI/text scale control remain pending. Android applies system-bar,
cutout, and keyboard insets to the SDL surface container, whose resize event
reaches the shared frame host. This uses the platform's
[window inset APIs](https://developer.android.com/reference/android/view/WindowInsets).
The pure menu layout tests also exercise inset, keyboard-occluded, and
enlarged-scale rectangles. Real cutout devices and keyboard flows still need
qualification.

```
scons release=1 -j8 responsive-menu-test mobile-input-test portable-renderer-test
mkdir -p build/responsive-menu-profile
GLOB2_USER_DATA_DIR="$PWD/build/responsive-menu-profile" ./build/darwin/client/release/src/responsive-menu-test
```

## Foreground and background behavior

The shared screen host consumes lifecycle events between frames. Backgrounding
suspends incremental screen updates, loading, presentation, and audio. Returning
excludes the background interval from simulation timing. Focus loss, rotation,
and child-screen admission cancel held gameplay/editor input. Renderer reset and
memory-pressure notifications invalidate GPU caches before the next foreground
draw. Browser visibility changes enter this same event path.

This covers incremental application screens. Remaining nested modal loops,
OS audio interruptions, transactional recovery saves, and multiplayer interruption
recovery still need implementation. A retained Android activity resuming is not
process-termination recovery.

The API 35 ARM64 emulator showed a System UI nonresponse dialog while concurrent
builds were active. It recovered after selecting Wait; this run establishes no
performance or thermal qualification.

## Progress and acceptance

- Implemented: mobile build identities, SDK diagnostics, target compiler
  discovery, verified dependency-prefix input, shared-source C++ build routing.
- Implemented: experimental SDL geometry renderer with texture lifecycle,
  primitives, alpha maps, clipping, HiDPI presentation, and capture.
- Implemented: pinned dependency bootstrap, Android staging/Gradle recipes,
  iOS Xcode generation, asset/writable roots, install/launch commands, compilation
  databases, and Android ELF architecture/alignment checks.
- Implemented: shared foreground/background timing, input cancellation, audio
  suspension, deferred graphics restoration, and Android orientation changes.
- Verified locally: Android ARM64, ARMv7, and x86-64 native libraries/staging;
  ARM64 release APK packaging, alignment, developer signing, installation, menu
  rotation, first tutorial launch, and retained-activity background/resume on the
  API 35 ARM64 emulator (2 GB, two virtual cores), with the installed package
  retained across a cold emulator restart. Full phone usability remains unqualified.
- Verified locally: 63 browser single-player/multiplayer checks across
  Chromium/Firefox/WebKit, plus nine targeted browser checks after the final
  modal-clock adjustment; 19 build-system checks; portable renderer restoration,
  clipping, alpha and resize checks; screen lifecycle and engine-session tests.
  The 50-tick engine fixture retains checksum `4056ae4d` after background and
  child-screen interruptions. Existing three AI baseline failures remain above.
- Unqualified: iOS compilation/launch and device signing (full Xcode is missing),
  ARMv7/x86-64 APK launch, IDE debugging, sanitizers, CI execution, OS-native
  WebSockets, and real devices.
- Implemented: responsive native main/editor entry menus, scrolling, wrapped
  labels, touch selection, mouse-event suppression, and Android surface insets.
  Menu touch dispatch, canceled drags, child viewport restoration, rotation,
  label wrapping, and letterbox clearing pass native integration checks. Nine
  focused browser regressions pass across Chromium, Firefox, and WebKit.
- Pending: viewport/camera integration, remaining phone layouts, gameplay gestures,
  touch editor tools, and touch tutorial adaptation.
- Pending: cooperative modal completion/Asyncify removal, remaining lifecycle
  coverage, durable persistence, rotating recovery saves, import/export, network
  recovery.
- Pending: mobile WebGL2, Safari/Chrome device qualification, cross-architecture
  replay/100,000-tick tests, 30-minute performance qualification.

The requested floor is iOS 15 and Android 8 on 2 GB phones, including ARMv7;
both orientations and 320x568 logical dimensions are required. Gameplay/editor
parity is required; voice, cloud saves, and store submission are deferred.
Keep the browser architecture contracts and all existing desktop checks.
