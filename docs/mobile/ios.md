# iOS development

Build and run the iOS app with isolated simulators and pinned dependencies.

## iOS

Build dependencies and the generated Xcode project with the same configuration:

```sh
python3 mobile/dependencies.py --target ios --environment simulator --release
python3 mobile/ios.py build --environment simulator --release
python3 mobile/ios.py install --environment simulator --release --device SIMULATOR_UUID
python3 mobile/ios.py launch --environment simulator --release --device SIMULATOR_UUID
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
