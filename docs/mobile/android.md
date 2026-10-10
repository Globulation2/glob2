# Android development

Build and install Android clients; use the release guides for Google Play and Amazon publishing.

## Android

Android builds support Android 7.0 (API 24) and newer. The application,
native dependencies and APK minimum must use the same minimum API; changing only
the APK manifest does not backport native libraries. Newer Java APIs need guarded
fallbacks. Keep the current compile/target SDK when supporting older devices.

Install Python, SCons, Git, make, autotools and pkg-config. Bootstrap the pinned
NDK and packaging tools, then build target dependencies and stage the project:

```sh
python3 mobile/setup_ndk.py
python3 mobile/setup_tools.py
sdk="$(python3 tools/dev_environment.py paths --field android_sdk)"
python3 tools/dev_environment.py sdkmanager -- --licenses
python3 mobile/setup_tools.py --sdk-packages
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
The F-Droid build mode adds `--fdroid` to the Android build, sign, install, and
launch commands. Both F-Droid and Play use `org.globulation2.glob2`. The
F-Droid release uses
`mobile/android-release.json` for its static version
name and base code. The three single-ABI APKs have codes `10 * base + 1` for
`armeabi-v7a`, `+ 2` for `arm64-v8a`, and `+ 3` for `x86_64`. `build --fdroid` checks the
APK's package name, version, ABI, alignment, native symbols, and indexed assets.
`mobile/android_release.py check` verifies the release manifest against the
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

The asset packager excludes local caches and metadata before writing its index.
The completed APK is checked against that index and its content digest, so an
AAPT-filtered or missing file fails the build instead of failing on first launch.
Release packaging restores gzip assets that AAPT expands and renames, then aligns
the APK before signing so compressed maps retain their indexed paths.
Native startup failures are also written to Android logcat under `SDL/APP`.


## Native tests on a connected Android device

After configuring the pinned [Android toolchain](toolchains.md), cross-compile the two doctest
binaries (`glob2-unit-tests` and `glob2-engine-tests`, from `test/tests.py`):

```sh
python3 mobile/dependencies.py --arch arm64-v8a --release
scons target=android arch=arm64-v8a release=1 android-tests -j4
python3 mobile/android_device_tests.py --serial DEVICE_SERIAL \
  --android-sdk "$ANDROID_SDK_ROOT" --output artifacts/android/device-tests
```

For an SDK outside the default task-local directory, pass `--android-sdk PATH`
to the Python build commands and `android_sdk=PATH` to SCons. Keep the dependency,
project and test architecture consistent. `--binary NAME` runs one of the two.
The runner checks the device ABI, stages stripped binaries and packaged assets in
an isolated `/data/local/tmp` directory, runs every case except the `[display]`
ones in-process, and records each exit code, log and JUnit report. It retains
those fixtures for diagnosis and never clears the installed app's data.

The engine binary reuses the production client objects. `mobile/NativeTestMain.cpp` supplies only the
shell platform bridges: filesystem assets, SDL main readiness, explicit app
metadata and absent Activity. Audio uses SDL's real dummy driver. Its event poll drains SDL's
queue without pumping the absent Java lifecycle queue. SDL3 renderer creation
waits for a Java Activity, so native dummy-video tools retain software rendering.
These tests execute
on the device CPU but do **not** establish real rendering, audio, native keyboard,
IME composition, lifecycle or physical gesture comfort. Test those separately in
the installed APK, including rotation, background/resume, save/load and editor
text entry. Keep device screenshots and logs with the test results.

[Mobile index](README.md) · [Documentation index](../README.md).
