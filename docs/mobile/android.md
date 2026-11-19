# Android development

Build and install Android clients; use the release guides for Google Play and Amazon publishing.

## Android

Android builds support Android 7.0 (API 24) and newer. The application,
native dependencies and APK minimum must use the same minimum API; changing only
the APK manifest does not backport native libraries. Newer Java APIs need guarded
fallbacks. Keep the current compile/target SDK when supporting older devices.

Install Python, CMake, Ninja, Git, make, autotools and pkg-config. Bootstrap the pinned
NDK and packaging tools, then build target dependencies and stage the project:

```sh
python3 platforms/mobile/setup_ndk.py
python3 platforms/mobile/setup_tools.py --glslang
sdk="$(python3 tools/dev_environment.py paths --field android_sdk)"
python3 tools/dev_environment.py sdkmanager -- --licenses
python3 platforms/mobile/setup_tools.py --sdk-packages
python3 platforms/mobile/dependencies.py --arch arm64-v8a --release
python3 platforms/mobile/android.py configure --arch arm64-v8a --release
```

`--glslang` builds the pinned, build-only glslang 15.1.0 (`buildsys/glslang.json`:
a SHA-256-verified source archive, built once into the development store) that
compiles the SDL GPU renderer's Vulkan SPIR-V. `android.py` provisions it on
demand when it is missing, or uses `GLOB2_GLSLANG` when that names the pinned
version, and fails rather than package an APK without the complete Vulkan shader
set; `build` and `bundle` then check the packaged SPIR-V against its receipts.
The Android SDL dependency is built with its `vulkan` feature
(`platforms/mobile/vcpkg.json`); without it SDL GPU reports the Vulkan driver as
unsupported and it cannot be selected.

Install the Android SDK platform and build-tools versions listed in the toolchain
manifest using `sdkmanager`, accepting the SDK license yourself. Then package:

```sh
python3 platforms/mobile/android.py build --arch arm64-v8a --release
python3 platforms/mobile/android.py sign --arch arm64-v8a --release
python3 platforms/mobile/android.py install --arch arm64-v8a --release --serial DEVICE_SERIAL
python3 platforms/mobile/android.py launch --arch arm64-v8a --release --serial DEVICE_SERIAL
```

Release builds start unsigned. `sign` uses a local developer key and validates
ZIP alignment, the signature and APK digest. Store distribution requires separate
signing arrangements. Use `armeabi-v7a` or `x86_64` for other supported targets.
Android packages store native libraries uncompressed and aligned so Android can
load them directly from the APK. The game library's linker export list exposes
only the SDL entry point, the Java document picker callback and the memory-trim
callbacks. Update
`platforms/mobile/android/libmain.exports` if Android or SDL needs to look up a
new entry point by name.
The F-Droid build mode adds `--fdroid` to the Android build, sign, install, and
launch commands. Both F-Droid and Play use `org.globulation2.glob2`. The
F-Droid release uses
`platforms/mobile/android-release.json` for its static version
name and base code. The three single-ABI APKs have codes `10 * base + 1` for
`armeabi-v7a`, `+ 2` for `arm64-v8a`, and `+ 3` for `x86_64`. `build --fdroid` checks the
APK's package name, version, ABI, alignment, native symbols, and indexed assets.
`platforms/mobile/android_release.py check` verifies the release manifest against the
desktop package version and requires the current version's tag, if it exists, to
point at the checked-out commit. Pull-request CI adds `--development`, which
accepts a version that was already tagged at an earlier commit (master keeps the
last released version until the next release bumps it) but still rejects version
codes that would not increase past any other tag. The signed output of `sign` is for development and
device testing; F-Droid signs its own published APKs.
The two stores use different signing keys, so switching stores requires
uninstalling the existing app and backing up or exporting saves first.
Omit `--release` consistently from both dependency and application commands for
debug builds. An explicit `JAVA_HOME` takes precedence over the shared pinned JDK.
`--android-sdk PATH` selects an explicit matching SDK; ambient Android SDK variables
do not replace the managed default. Use `GLOB2_DEV_MODE=isolated` for checkout-local
tools and uncached dependency builds, including independent release verification.
See [development storage](../development/development-storage.md#shared-development-storage)
for overrides, cache budgets, reporting, and migration. Completed dependency
bundles include pinned SDL Java sources; packaging never needs discarded vcpkg
build intermediates.
Release Android packages use the source package version as `versionName` by
default; pass `--version-name` to choose a different store-facing value.
The `--version-code` argument is carried into the generated Gradle project.
For the separate mainland China local-play configuration, pass `--china` to
both the dependency and Android packaging commands. The iOS dependency and
packaging commands accept the same flag.

The staged Gradle project allows a 2 GiB Java heap for APK packaging, which
retains the full artwork bundle while writing the archive. Native compilation
keeps its separate `--jobs` setting; Gradle packaging uses two workers.

The asset packager excludes local caches and metadata before writing its index.
The index has a 4 MiB limit shared by packaging validation and the native startup
reader; packaging rejects an oversized index before producing an unusable APK.
The completed APK is checked against that index and its content digest, so an
AAPT-filtered or missing file fails the build instead of failing on first launch.
Release packaging restores gzip assets that AAPT expands and renames, then aligns
the APK before signing so compressed maps retain their indexed paths.
Native startup failures are also written to Android logcat under `SDL/APP`.


## Native tests on a connected Android device

After configuring the pinned [Android toolchain](toolchains.md), cross-compile the two doctest
binaries (`glob2-unit-tests` and `glob2-engine-tests`, from `test/tests.py`):

```sh
python3 platforms/mobile/dependencies.py --arch arm64-v8a --release
python3 tools/dev_build.py target=android arch=arm64-v8a release=1 android-tests -j4
python3 platforms/mobile/android_device_tests.py --serial DEVICE_SERIAL --arch arm64-v8a \
  --output artifacts/android/device-tests
```

The runner uses the managed SDK unless `--android-sdk PATH` names another, and the
dependency prefix the binaries were linked against unless `--mobile-deps PATH`
does. Keep the dependency, project and test architecture consistent.
`--binary NAME` runs one of the two; `--suite`, `--filter`, `--tag` and
`--exclude-tag` select cases as `test/run_tests.py` does.

The runner checks the device ABI and stages, in an isolated `/data/local/tmp`
directory, the stripped binaries, the runtime tree the APK ships (exported by
`android.py`'s packager, so sprites are the packaged WebP files) and the
repository fixtures the cases read through `glob2test::sourceRoot()`. Like the
desktop runner it runs every case in its own process with a fresh profile and a
timeout: four times the desktop runner's limit from the recorded CI durations
(`--timeout-scale`), or `--case-timeout SECONDS` for every case. Cases run in
chunks of 20 per device invocation (`--chunk`), so a hang or crash costs one case.
If the device reboots during a chunk, its results are discarded and the cases run
again one at a time; a case during which the device reboots twice is recorded as
a crash. After every chunk the runner writes `result.json`, one JUnit file per
binary and each case's log under `logs/`; `--resume` continues an interrupted run
in the same `--output`. At the end it retrieves the artifacts the cases wrote.
It retains the remote directory after any failure and never clears the
installed app's data.

The default selection matches CI's: `[display]` cases need a real window and
are reported as skipped, `[slow]` cases run only with `--slow`, and
`[map-generators]` cases, which CI runs in a separate Linux job, are left out
unless selected with `--tag map-generators`.

The engine binary reuses the production client objects. `platforms/mobile/NativeTestMain.cpp`
supplies only the shell platform bridges: filesystem assets, SDL main readiness,
explicit app metadata, an absent Activity and SDL's Android lifecycle queue. SDL
creates that queue from Java, and renderer creation and event pumping wait on it;
the bridge creates it through SDL's own `nativeSetupJNI` with a minimal stand-in
VM that offers no Java environment afterwards. Audio uses SDL's real dummy
driver and rendering the software renderer. These tests execute on the device CPU
but do **not** establish GPU rendering, audio, native keyboard, IME composition,
lifecycle or physical gesture comfort. Test those separately in the installed
APK, including rotation, background/resume, save/load and editor text entry.
Keep device screenshots and logs with the test results.

### Emulator

`platforms/mobile/emulator.py` runs the pinned emulator and system image
(`platforms/mobile/emulator.json`) with its own ADB server, so it never attaches
to USB devices or other emulators:

```sh
python3 platforms/mobile/emulator.py configure --arch x86_64
python3 platforms/mobile/emulator.py adb-server &
python3 platforms/mobile/emulator.py run --arch x86_64 &
python3 platforms/mobile/emulator.py status --arch x86_64   # prints 1 once booted
python3 platforms/mobile/android_device_tests.py --serial emulator-5580 --adb-port 15037 --arch x86_64
python3 platforms/mobile/emulator.py stop --arch x86_64
```

The AVD and the emulator's state live under `build/mobile-tools/`. The emulator
renders with SwiftShader on the CPU (`-gpu swiftshader`), so it does not
use the host GPU, and needs KVM: on Linux it cannot run while a VirtualBox VM
holds the virtualization extensions. On a two-core emulator the full unit and
engine suites take several hours.

[Mobile index](README.md) · [Documentation index](../README.md).
