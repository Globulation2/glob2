# Platforms verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Android device execution

Selected client harnesses can run as native Android executables on a connected
device. See the [device build and runner
commands](../../mobile/development.md#build-and-validate).
Shell tests use SDL dummy video and have no audio or Java Activity; installed-APK
interaction and lifecycle tests are separate. `python3 test/test_mobile_asset_bundle.py`
checks the asset-index packaging regression independently of an Android SDK.

## Phone interface verification

The native portable-renderer checks exercise real SDL touch dispatch in portrait
and landscape, including placement/confirmation, camera gestures, building
controls, replay actions, setup/settings navigation and modal viewport changes.
Build and run commands are in [Mobile development](../../mobile/development.md#build-and-validate).
The `GameGUITouch` and `UIPresentation` cases need a windowing display (Xvfb on
Linux), run in isolated profiles and copy their screenshots into their artifact
directories. `UIPresentation` has one case per viewport, so CI shards distribute
the full screen/presentation/inset sweep and each viewport gets its own timeout
and failure report. Its offline lobby fixture renders the production screen
without starting the public IRC connection, so layout checks do not wait for
external network timeouts during teardown. They complement
Android/iOS device playtesting; they do not establish device lifecycle,
performance, keyboard or cross-platform simulation compatibility.
