# Amazon Appstore releases

Build and submit the Fire tablet edition, which supports offline and LAN play.

## Amazon Appstore Fire tablet release

The Amazon candidate is one release APK with `arm64-v8a` and `armeabi-v7a`
libraries and an Android 7.0 (API 24) minimum. Amazon release qualification
remains limited to Fire OS 7/8 tablets; Fire OS 5/6 are not qualified. Build both dependency
sets, then package the APK from the arm64 configuration:

```sh
python3 mobile/dependencies.py --arch arm64-v8a --release
python3 mobile/dependencies.py --arch armeabi-v7a --release
python3 mobile/android.py build --arch arm64-v8a --release --amazon-apk \
  --version-code "$(python3 mobile/amazon_release.py version-code)"
```

Supporting Fire OS 5/6 requires a separate compatibility change: lower the Android API
floor only after updating the build identity and native dependency triplets,
auditing platform API use, and playing on representative older tablets. Do not
select those devices in the store until they pass.

`--amazon-apk` builds an isolated Amazon native flavor that hides and blocks
online play (the online hub and invite links) while retaining LAN play. It reuses the standard
Android dependency builds and checks matching native libraries in both ABIs,
the packaged asset index, alignment and both native build IDs. It derives
`versionName` from
`PACKAGE_VERSION` in `scons/build_layout.py`. The four version components map
to one increasing Android `versionCode`; never reuse or lower a code already
submitted to Amazon. A release build remains unsigned until the separate
release workflow signs it. The [local Android `sign` command](../mobile/android.md#android) is for developer
installs and must not be used as a store identity.

The public `.github/workflows/amazon-appstore.yml` runs only when mirrored to
`genixpro/glob2-release`; its environment secrets remain private.
Its manual dispatch selects a public `vVERSION` tag that resolves to
the same commit in the release mirror. `build` produces a
verified unsigned APK without credentials. `candidate` signs it and retains a
short-lived APK for the first manual console submission. `publish` performs the
same build and signing, then submits an update through Amazon's App Submission
API. The workflow does not create the first listing. Protect the release
repository's `master` branch and release tags in both repositories; restrict dispatch and
environment access to maintainers. Configure these private environment values:

After the public change and its release tag are reviewed and merged, fetch the
public tag into the release mirror and push that exact tag ref there. The
selected tag must be reachable from public `master` and point to the same
source commit in both repositories. Do not recreate it on the mirror's merge
commit. Dispatch the workflow from mirror `master` only after that merge is
present there.

| Name | Type | Purpose |
| --- | --- | --- |
| `GLOB2_AMAZON_KEYSTORE_BASE64` | secret | Base64 PKCS#12 upload keystore |
| `GLOB2_AMAZON_STORE_PASSWORD`, `GLOB2_AMAZON_KEY_PASSWORD` | secrets | Upload-key passwords |
| `GLOB2_AMAZON_CLIENT_ID`, `GLOB2_AMAZON_CLIENT_SECRET` | secrets | App Submission API security profile |
| `GLOB2_AMAZON_APP_ID` | variable | App ID from the Developer Console |
| `GLOB2_AMAZON_CERT_SHA256` | variable | SHA-256 digest of the upload certificate |

Generate the upload keystore on a trusted machine with `keytool -genkeypair
-storetype PKCS12 -keystore glob2-amazon.p12 -alias glob2-amazon -keyalg RSA
-keysize 3072 -validity 10000`; enter passwords interactively. Record its
certificate SHA-256 from `keytool -list -v -keystore glob2-amazon.p12` as 64
hex digits without colons, back up
the keystore securely, and encode it for the environment secret without adding
it to Git or workflow logs. In the Amazon Developer Console, create an App
Submission API security profile and attach it to that API before storing its
client ID and secret. The App ID exists only after the first app is created.

Register an Amazon Developer account, complete identity checks, and create the
first app version in the Developer Console. Use package `org.globulation2.glob2`,
price Free, and disable optional Amazon DRM. Select only Fire tablets that pass
qualification. Complete the privacy questionnaire based on the actual network
and account behavior, link the [Fire tablet privacy policy](../mobile/amazon-privacy-policy.md),
and supply a support contact, icon, and Fire-device
screenshots. Draft listing copy: **Globulation 2** — “Build and guide a colony in
an open-source real-time strategy game. Set priorities for your workers, gather
resources, construct buildings, explore maps, and compete with other colonies.”
Review this copy against the final Fire build and Amazon's listing fields before
submission. Amazon requires the first version through the console; the API can
submit later versions. Keep the first signed APK and its certificate identity
for future update verification.

Before the first submission, play the signed APK on a Fire OS 7 and a Fire OS 8
tablet, including a 32-bit device where relevant. Check install, start, an
entire match, touch placement and painting, keyboard/IME entry, audio, rotation,
background/resume, save/load and network play. Record device models, Fire OS
versions, screenshots, logs, saves and replay/checksum evidence under
`artifacts/`, and inspect the supported-device list in the Developer Console.
After the first version is live, run one controlled API update and check that a
store-delivered update retains local saves. API review status and rejected
submissions require console follow-up; the workflow deliberately leaves an
open edit untouched if a submission fails.

[Release index](README.md) · [Documentation index](../README.md).
