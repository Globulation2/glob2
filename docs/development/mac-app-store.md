# Mac App Store release

The Mac App Store release path uses the native SCons build, then stages a separate
sandboxed app. Direct-distribution DMGs use `scons release=1 package` and are not
used for App Store uploads. Apple requires the App Sandbox entitlement and a
signed installer package for the Mac App Store.

## Local candidate

On macOS, run:

```sh
scons release=1 bundle -j2
python3 darwin/package_app_store.py --build 1
```

The default ad hoc signature is for local sandbox testing. The candidate is in
the ignored `artifacts/mac-app-store/Glob2.app`. Check launch, settings, saves,
map import, LAN hosting, and YOG sign-in and connection. The app's profile data
is placed inside its sandbox container. Existing direct-distribution profiles in
`~/.glob2` do not migrate automatically, so migration needs a separate user-facing
decision before release to existing desktop users. The generated Mac icon includes
required large sizes, but those are enlarged from the existing 128-pixel artwork
and need visual review.

## Manual GitHub Actions release

Keep this workflow and the packaging code in the public `Globulation2/glob2`
repository. Changes to the workflow, SCons bundle code, or `darwin/` trigger a
build-only pull-request check there. A public run cannot enter the signing job,
even if someone manually requests `upload`: the build job rejects that request,
and the signing job separately requires the private repository, `master`, a
manual dispatch, and `upload=true`. Do not configure Mac release secrets in the
public repository.

For a release, first sync the reviewed public source to the owner-only private
mirror, `genixpro/glob2-release`, on `master`. The owner then runs **Mac App
Store release** manually from that mirror's Actions tab. Enter a `build_number`
larger than every previously uploaded build number. Leave `upload` off to
compile and smoke-test an ad hoc sandboxed candidate. Set `upload` to true to
sign the app, build a `.pkg`, validate it with Apple, and upload it to App Store
Connect. The public version comes from `PACKAGE_VERSION` in
`scons/build_layout.py`, so update it in the reviewed source before syncing.
The workflow does not submit an uploaded build for App Review; processing and
submission are separate actions in App Store Connect.

The build job runs on GitHub's arm64 `macos-26` runner with Xcode 26 or newer. It
installs the native Homebrew dependencies, runs `scons release=1 bundle`, stages
the sandboxed app, checks its signature, lists generators, and writes a map in
its container. The private mirror's signed job uses the exact app artifact from
that run's build job. Both the source app archive and signed `.pkg` are retained
as workflow artifacts for 14 days in the repository where the run occurred.

### One-time App Store Connect setup

Add macOS to the existing iPhone App Store Connect record using its registered
bundle ID, `org.globulation2.glob2`. The App Store staging step writes that ID
into its app copy; the existing direct-distribution bundle is unchanged. The
optional mirror repository variable `APPLE_BUNDLE_ID` can override the ID if
the App Store Connect setup changes. Set these variables only on the private
mirror to the exact names of the certificates imported into the workflow
keychain:

| Variable | Value |
| --- | --- |
| `APPLE_APP_SIGN_IDENTITY` | Apple Distribution application identity |
| `APPLE_INSTALLER_SIGN_IDENTITY` | Mac Installer Distribution identity |
| `APPLE_PROVIDER_PUBLIC_ID` | Optional App Store Connect provider ID if the API key has multiple providers |

Create the `mac-app-store` GitHub environment **on the private mirror** and
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

The workflow creates a temporary keychain, imports the certificates, and removes
the temporary keychain and profile after the job. Apple Distribution signs the
app; Mac Installer Distribution signs the `.pkg`. The API key authenticates
`altool` validation and upload. The app record, bundle ID, profile, signing
certificates, and API key must belong to the same developer team.

The workflow itself can be checked with `upload` off before adding the release
secrets. A real signed upload from the private mirror remains the final
integration test; an ad hoc local build and an unsigned installer package do
not prove App Store acceptance.
