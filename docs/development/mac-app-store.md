# Mac App Store release

The Mac App Store release path uses the native SCons build, then stages a separate
sandboxed app. Direct-distribution DMGs use `scons release=1 package` and are not
used for App Store uploads. Apple requires the App Sandbox entitlement and a
signed installer package for the Mac App Store.

## Local build check

On macOS, run:

```sh
scons release=1 -j2
```

This compiles the game without packaging or release signing it. On Apple silicon,
the linker may add an ad hoc signature to the raw executable. To produce a sandboxed
candidate, use a manual run in the release mirror with `upload` off, then download
that run's app artifact. Check launch, settings, saves, map import, LAN hosting,
and YOG sign-in and connection. The app's profile data is placed inside its
sandbox container. Existing direct-distribution profiles in `~/.glob2` do not
migrate automatically, so migration needs a separate user-facing decision before
release to existing desktop users. The generated Mac icon includes required large
sizes, but those are enlarged from the existing 128-pixel artwork and need visual
review. Apple Distribution-signed App Store builds cannot be launched directly
before Apple delivers them; test the ad hoc candidate locally and the processed
distribution build through TestFlight.

## Manual GitHub Actions release

Keep this workflow and the packaging code in the public `Globulation2/glob2`
repository. Changes to the workflow, SCons bundle code, or `darwin/` trigger a
compile and raw-binary smoke test on public pull requests. Public manual runs and
release-mirror pull requests skip the build job. Bundling, sandbox staging, and
signing occur only on a manual run from the release mirror's `master` branch,
started and rerun by its owner, `genixpro`. The upload job also requires that
repository, branch, event, owner, and `upload=true`.
Do not configure Mac release secrets in the upstream `Globulation2/glob2`
repository. Keep them in the mirror's restricted `mac-app-store` environment.

For a release, first sync the reviewed public source to the owner-controlled
release mirror, `genixpro/glob2-release`, on `master`. The mirror is public;
keep its write access limited to the owner. Workflow logs and artifacts in that
mirror are public, so never print credentials or include them in artifacts.
The owner then runs **Mac App
Store release** manually from that mirror's Actions tab. Enter a `build_number`
larger than every previously uploaded build number. Leave `upload` off to
compile and smoke-test an ad hoc sandboxed candidate. Set `upload` to true to
sign the app, build a `.pkg`, validate it with Apple, and upload it to App Store
Connect. The public version comes from `PACKAGE_VERSION` in
`scons/build_layout.py`, so update it in the reviewed source before syncing.
The workflow does not submit an uploaded build for App Review; processing and
submission are separate actions in App Store Connect.

Both build paths run on GitHub's arm64 `macos-26` runner with Xcode 26 or newer.
The current App Store package declares macOS 26.0 as its minimum because the
arm64 game and bundled Homebrew libraries are built on that runner. Supporting
older macOS versions requires building and testing the entire dependency set
with an older deployment target.
The public check compiles the game and lists generators from the raw binary. It
does not run `codesign` or use any release signing identity.
The release mirror run also bundles the app, stages its sandboxed candidate,
checks the signature, lists generators, and writes a map in its container. Its
signed job uses the exact app artifact from that run's build job. The unsigned
source app archive is retained as a release mirror workflow artifact for 14 days.
The signed `.pkg` is sent to Apple, not published as a public workflow artifact.

### One-time App Store Connect setup

Add macOS to the existing iPhone App Store Connect record using its registered
bundle ID, `org.globulation2.glob2`. The App Store staging step writes that ID
into its app copy; the existing direct-distribution bundle is unchanged. The
optional mirror repository variable `APPLE_BUNDLE_ID` can override the ID if
the App Store Connect setup changes. Set these variables only on the release
mirror to the exact names of the certificates imported into the workflow
keychain:

| Variable | Value |
| --- | --- |
| `APPLE_APP_SIGN_IDENTITY` | Apple Distribution application identity |
| `APPLE_INSTALLER_SIGN_IDENTITY` | Mac Installer Distribution identity |
| `APPLE_PROVIDER_PUBLIC_ID` | Optional App Store Connect provider ID if the API key has multiple providers |

Create the `mac-app-store` GitHub environment **on the release mirror** and
restrict deployments to its `master` branch. Add these **environment secrets on
that mirror only**. Base64 values must be single-line encodings of the original
binary files; do not commit them to either repository. Required environment
reviewers can add another release gate.

| Secret | Value |
| --- | --- |
| `APPLE_APP_P12_BASE64` | Exported Apple Distribution certificate and private key (`.p12`) |
| `APPLE_APP_P12_PASSWORD` | Password for that `.p12` |
| `APPLE_INSTALLER_P12_BASE64` | Exported Mac Installer Distribution certificate and private key (`.p12`) |
| `APPLE_INSTALLER_P12_PASSWORD` | Password for that `.p12` |
| `APPLE_PROFILE_BASE64` | Mac App Store distribution provisioning profile |
| `APPLE_API_KEY_P8_BASE64` | App Store Connect team API private key (`.p8`) |
| `APPLE_API_KEY_ID` | API key ID |
| `APPLE_API_ISSUER_ID` | API issuer ID |

Use an App Store Connect API key with the Developer role for uploads. Team keys
apply to every app in the team, so keep the key in this environment and revoke it
if exposed. Export the `.p12` files in a format accepted by macOS Keychain and
test a `security import` into a temporary keychain before storing their base64
encodings. OpenSSL 3's default PKCS#12 encryption may be rejected by Keychain;
`openssl pkcs12 -export -legacy` produced an importable export during setup.

The workflow creates a temporary keychain, imports the certificates, and removes
the temporary keychain and profile after the job. Apple Distribution signs the
app; Mac Installer Distribution signs the `.pkg`. The API key authenticates
`altool` validation and upload. The app record, bundle ID, profile, signing
certificates, and API key must belong to the same developer team.

The workflow itself can be checked with `upload` off before adding the release
secrets. A real signed upload from the release mirror remains the final
integration test; an ad hoc local build and an unsigned installer package do
not prove App Store acceptance.

### TestFlight and export compliance

After Apple processes a signed upload, enter **What to Test** on the macOS
TestFlight build, resolve its export compliance status, and add the build to an
internal testing group. Add the intended App Store Connect user as an internal
tester in that group, then accept the TestFlight invitation and install the
Mac build. This does not submit an App Store version for review or release it.

The optional secure WebSocket client uses OpenSSL for standard TLS outside
Apple's operating-system encryption. Declare that use accurately in App Store
Connect. If the app will be distributed in France, Apple requires a French
encryption declaration; see [Apple's export compliance table](https://developer.apple.com/help/app-store-connect/reference/app-information/export-compliance-documentation-for-encryption).
The supplier must complete and sign the official declaration and submit the
required supporting material to ANSSI under its
[filing instructions](https://cyber.gouv.fr/reglementation/reglementation-identite-confiance-numerique/controles-reglementaires-cryptographie/controle-moyen-de-cryptologie/controle-reglementaire-cryptographie-formulaires/).
Upload the resulting declaration documentation in App Store Connect and wait
for Apple's review before distributing the build to testers. Keep personal
supplier details, signed forms, and Apple approval material outside the public
repository and public release workflow artifacts.

For the Mac product page, use authentic screenshots from the app at one of
Apple's accepted Mac sizes. The listing, review notes, and screenshot uploads
are draft metadata; **Add for Review** and production release are separate
actions.
