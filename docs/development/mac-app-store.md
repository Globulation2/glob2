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

Run **Mac App Store release** from the Actions tab. Enter a `build_number` larger
than every previously uploaded build number. Leave `upload` off to compile and
smoke-test an ad hoc sandboxed candidate without release credentials. Set
`upload` to true to sign the app, build a `.pkg`, validate it with Apple, and upload
it to App Store Connect. The public version comes from `PACKAGE_VERSION` in
`scons/build_layout.py`, so update it in the release source before dispatching.
Uploads are allowed only from `master`. The workflow
has no push or pull-request trigger, and it does not submit the uploaded build
for App Review. Processing and submission are separate actions in App Store
Connect. The `mac-app-store` GitHub environment can be configured with required
reviewers for an additional release gate.

The build job runs on GitHub's arm64 `macos-26` runner with Xcode 26 or newer. It
installs the native Homebrew dependencies, runs `scons release=1 bundle`, stages
the sandboxed app, checks its signature, lists generators, and writes a map in
its container. The signed job runs only when `upload` is true and uses the exact
app artifact from the build job. Both the source app archive and signed `.pkg`
are retained as workflow artifacts for 14 days.

### One-time App Store Connect setup

Create the macOS app record and a matching registered bundle ID. Set repository
variable `APPLE_BUNDLE_ID` if it differs from `com.globulation2.Glob2`. Set these
repository variables to the exact names of the certificates imported into the
workflow keychain:

| Variable | Value |
| --- | --- |
| `APPLE_APP_SIGN_IDENTITY` | Apple Distribution application identity |
| `APPLE_INSTALLER_SIGN_IDENTITY` | Mac Installer Distribution identity |
| `APPLE_PROVIDER_PUBLIC_ID` | Optional App Store Connect provider ID if the API key has multiple providers |

Create the `mac-app-store` GitHub environment and add these **environment
secrets**. Base64 values must be single-line encodings of the original binary
files; do not commit them to the repository.

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
secrets. A real signed upload remains the final integration test; an ad hoc local
build and an unsigned installer package do not prove App Store acceptance.
