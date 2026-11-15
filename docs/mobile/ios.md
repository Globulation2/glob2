# iOS development

Build and run the iOS app with isolated simulators and pinned dependencies.

## iOS

Build dependencies and the generated Xcode project with the same configuration:

```sh
python3 platforms/mobile/dependencies.py --target ios --environment simulator --release
python3 platforms/mobile/ios.py build --environment simulator --release
python3 platforms/mobile/ios.py install --environment simulator --release --device SIMULATOR_UUID
python3 platforms/mobile/ios.py launch --environment simulator --release --device SIMULATOR_UUID
```

The simulator tools use an isolated device set under
`build/mobile-tools/ios-simulators`. Simulator builds ad hoc sign and verify the
completed app bundle without Apple credentials. Device builds use `--environment device` and
require either `--team TEAM_ID` with local provisioning or `--unsigned` for a
compile-only build. An unsigned device app cannot be installed. Release symbols
are retained alongside the application. With `--team`, Xcode must have the Apple
Developer account added in Settings > Accounts; the build allows Xcode to create
or update the provisioning profile for the bundle ID. The generated target is
included in Xcode archives for TestFlight distribution.

The iOS SDL build excludes camera capture and HID/Bluetooth device discovery,
which Glob2 does not use. Dependency cache identities include overlay recipes
and patches, so changes to those backends cannot reuse an older library bundle.

The protected TestFlight upload job waits up to 30 minutes for Apple's processing
of the exact build number and marketing version. External candidates must become
App Store eligible with resolved encryption metadata before successful upload
provenance is recorded. An upload transport success alone is insufficient;
rejected or invisible builds fail the job and require checking Apple's delivery
notification before retrying with a corrected immutable tag.


## Safe-area layout

Menus, dialogs and the touch HUD use UIKit safe-area insets to keep text and
controls clear of the status bar, camera cutout and home indicator. Backgrounds
can extend to the screen edges. The shared host bridge uses SDL3's
`SDL_PLATFORM_IOS` platform guard; the former SDL2 `__IPHONEOS__` macro is not
defined by SDL3 or the iOS compiler. Insets are read from the current window so
layouts can follow orientation and system UI changes.

## Reusing an empty App Store draft

When Apple already has a listing draft for an earlier candidate, the owner can
set `draft_version_id` in `ios-production.yml` while selecting `prepare`.
The pipeline requires a processed, App Store eligible build from the exact
selected public tag. It verifies that the specified draft belongs to Glob2,
targets iOS, is `PREPARE_FOR_SUBMISSION`, and has no attached build before
updating its version and attaching that build with manual release control.
The existing listing, screenshots and review contact remain on the same draft.
It refuses to replace an attached build or reuse a different matching version.
Leaving the input empty retains the normal exact-version preparation behavior.
This option neither submits for review nor publishes the app.

## Signing fingerprints for invite links

`.github/workflows/app-signing-fingerprints.yml` reads the public signing
identities that [mobile app links](../hosting/networking.md#mobile-app-links) need. Like
the release workflows, every job runs only when the owner dispatches it from
`master` in `genixpro/glob2-release`; it is skipped in `Globulation2/glob2`. Sync
the mirror using the [release mirror procedure](../releases/releasing.md#the-release-mirror), then run **Actions → App signing fingerprints → Run
workflow** and approve its three environments. The run summary lists:

### `android-play` (`google-play-internal`)

The upload key's certificate SHA-256, read with `keytool -list` from the keystore secret decoded to a private runner directory and then shredded. The Play App Signing certificate: the latest **internal** release's version code (from a Play edit that is deleted, never committed), then `generatedapks.list` and one downloaded generated APK, whose signer `apksigner` reports. If generated APKs are unavailable it falls back to a `systemapks.variants` APK, which Play signs with the same key.

### `android-amazon` (`amazon-appstore`)

The Amazon key's certificate SHA-256, compared with `GLOB2_AMAZON_CERT_SHA256`. The Fire edition has no invite links, so it is not needed for app links.

### `ios` (`ios-testflight`)

The `org.globulation2.glob2` App ID's Team ID (its `seedId`) and capabilities, and each provisioning profile's state and whether it carries the associated-domains and multicast entitlements. It enables `ASSOCIATED_DOMAINS` when missing, unless the dispatch input is unchecked; a second run changes nothing.

### `report`

The `appLinks` block for `instance.yaml` with the Play App Signing certificate and the App ID.


It prints only certificate fingerprints and App ID metadata, never keys,
passwords or tokens. Play's Workload Identity provider must accept this
workflow (`genixpro/glob2-release/.github/workflows/app-signing-fingerprints.yml`
at `refs/heads/master`) as well as the internal release workflow. Otherwise the
Play step is reported as not read and the job fails after printing the upload key.
The Play App Signing certificate is then in Play Console > Test and release > App
integrity.

Enabling a capability invalidates the App ID's existing profiles; the next
TestFlight export with `-allowProvisioningUpdates` regenerates the Xcode-managed
App Store profile with the capability. The TestFlight archive is built unsigned,
so the app's entitlements file does not reach the exported app, and the profile
alone does not add `applinks:`. Check a TestFlight IPA with
`codesign -d --entitlements - Payload/Glob2.app` before relying on universal links.

[Mobile index](README.md) · [Documentation index](../README.md).

### Vulkan gradient computation

The iOS project embeds the matching hash-pinned MoltenVK 1.4.2 framework and its
Apache-2.0 notice for device or simulator builds. Xcode signs the embedded framework
on copy. Gradient compute loads it independently of rendering, and falls back to
CPU for unavailable device capabilities. The lock is `buildsys/moltenvk.json`; no
runtime shader compiler is shipped. Device correctness and signed package launch
need Apple qualification in addition to cross-platform solver tests.

### SDL GPU Metal shaders

The SDL GPU renderer on iOS needs offline Metal libraries (`.metallib`); the
pinned SDL GPU does not support the Simulator, so simulator packages simply lack
it. Device packaging (`ios.py configure|build --environment device`) fails unless
the build directory holds the complete Metal set, produced by importing a verified
translated bundle (`GLOB2_RENDER_SHADER_INPUT`, generated on Linux with pinned
glslang and SPIRV-Cross) and compiling it with `GLOB2_METAL_SDK` plus a verified
`GLOB2_METAL_TOOL_LOCK`; see [Portable shader build artifacts](../architecture/rendering.md#portable-shader-build-artifacts).
`--allow-missing-sdl-gpu` packages portable SDL only and says so. A successful
device build verifies the app's Metal libraries against their receipts.

#### The Metal tool lock

`platforms/mobile/metal-tool-lock.json` is the trust anchor for the Apple compiler
that produces shipped Metal libraries. It holds the exact `xcodebuild -version`
output, the SDK name and version, and SHA-256 digests of the resolved `metal` and
`metallib` executables, in the format `tools/shaders/generate.py` reads. It only
changes through a reviewed pull request: no workflow commits, regenerates or
overrides it, and release builds have no switch that skips it.

To create or bump the lock (first time, or after an Xcode pin or runner image change):

1. Run the **iOS Metal tool lock** workflow
   (`.github/workflows/ios-metal-tool-lock.yml`, manual dispatch only) on the
   revision whose `platforms/mobile/toolchain.json` pin should be locked. It runs on
   the `xcode-27` image, refuses any Xcode other than the pin, and uploads a
   `metal-tool-lock` artifact (the lock, the source commit and, when a lock is
   already committed, the differences). It has read-only repository access and no
   secrets.
2. Review the artifact: `xcodeVersion` must start with `Xcode` and the pinned
   version, `sdk` must be `iphoneos`, and an Xcode bump should explain any changed
   digest. On a Mac with the same Xcode,
   `python3 tools/shaders/metal_tool_lock.py --output /tmp/lock.json` reproduces it.
3. Commit it as `platforms/mobile/metal-tool-lock.json` in a pull request (with the
   `platforms/mobile/toolchain.json` change, for an Xcode bump).

`python3 tools/shaders/metal_tool_lock.py --check` verifies the committed lock
against the local toolchain and prints each differing field. A mismatch means the
Xcode, SDK or compiler on that machine is not the one the repository reviewed, most
often because the runner image moved; no Metal library is compiled with it, so
regenerate and review the lock rather than working around it.

#### TestFlight

The TestFlight workflow translates the shaders in a Linux job with the pinned glslang
and SPIRV-Cross, without secrets, and passes the archive to the Mac build job with its
SHA-256 as a job output. The Mac job checks that digest, re-verifies the bundle
against its own checkout with the `--prebuilt` import, checks the committed lock, and
builds with `GLOB2_RENDER_SHADER_INPUT`, `GLOB2_METAL_SDK=iphoneos` and
`GLOB2_METAL_TOOL_LOCK`; any mismatch fails the build. The signing job never handles
shader input. A tagged source without a committed lock packages portable SDL only
(`--allow-missing-sdl-gpu`) with a warning naming the lock workflow. The
`ios-production` workflow promotes an uploaded build and does not rebuild shaders.
