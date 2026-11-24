# Amazon Appstore releases

Build and submit the Fire tablet edition, which supports offline and LAN play.

## Amazon Appstore Fire tablet release

The Amazon candidate is one release APK with `arm64-v8a` and `armeabi-v7a`
libraries and an Android 7.0 (API 24) minimum. Amazon release qualification
remains limited to Fire OS 7/8 tablets; Fire OS 5/6 are not qualified. Build both dependency
sets, then package the APK from the arm64 configuration:

```sh
python3 platforms/mobile/dependencies.py --arch arm64-v8a --release
python3 platforms/mobile/dependencies.py --arch armeabi-v7a --release
python3 platforms/mobile/android.py build --arch arm64-v8a --release --amazon-apk \
  --version-code "$(python3 platforms/mobile/amazon_release.py version-code)"
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
`PACKAGE_VERSION` in `buildsys/build_layout.py`. The four version components map
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
screenshots. Use the storefront-v2 English listing below.

**Short description**

```text
Real-time strategy with no ads and no in-app purchases. Grow a colony and fight rivals for land. You set the priorities; your colony does the work.
```

**Full description**

```text
Real-time strategy where you command the colony, not every unit.
Free. No ads. No in-app purchases. No loot boxes. No premium currency.

Globulation 2 is a real-time strategy game about a colony of globs: small, busy creatures that work, eat, train and fight. You grow the colony, keep it fed and fight rival colonies that want the same land.

You decide. Your globs do the work.
Choose what to build and how many globs should work on it. They find their own way, gather the materials and build it, and they eat when they are hungry. To explore new land or attack a rival, place a flag and set how many globs should go. There are no units to select one by one: you plan for the whole colony.

Keep the colony fed.
Inns feed your globs. Swarms produce more of them. Schools, racetracks, swimming pools and barracks train them, and hospitals heal them. All of it competes for the same workers, so every new project takes workers from another: a bigger army is no use if nobody is left to harvest the wheat.

Plan where to expand.
Wheat, wood and stone are rarely where you want them, and the routes between them matter as much as the resources. Decide where to expand, what to protect with walls and what is worth fighting for. Play 23 handmade maps, or generate a new map from any of 67 random landscapes.

Fight for the land.
Rival colonies are growing too, and they want the same land. Train warriors, build defense towers and stone walls, and place war flags where your army should attack. An attack lasts only as long as the food and hospitals that support it, so a war is won at the inns as much as at the front.

What's in the game.
• 23 handmade maps, plus new maps generated from 67 random landscapes
• 8 computer opponents, from easy to hard
• Up to 16 colonies in one match
• A 10-lesson tutorial
• Built-in map editor
• Save and watch replays
• Offline single-player, with no account needed

Play together.
Play over your local network with no account and no internet connection.

Open source, built by volunteers.
Globulation 2 started in 2001 and was released to the public in 2004. Volunteers have built it over two decades. It is free and open source under the GPL, version 3: the code is public, and anyone can read it, change it and help improve the game.

This Fire tablet edition is for offline and local network play. It has no online multiplayer and no accounts.
```

**Feature bullets**

- Command the colony, not every unit: you choose what to build and where to attack, and your workers and warriors do the work on their own
- 23 handmade maps, plus new maps generated from 67 random landscapes
- 8 computer opponents, from easy to hard
- Offline single-player, with no account needed
- Free. No ads. No in-app purchases. No loot boxes. No premium currency.

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
open edit untouched if a submission fails. A later `publish` of the same tag
resumes only its own edit, one whose single APK already carries this release's
version code (the upload finished and a later step failed); any other open
edit, including one interrupted mid-upload, must be reconciled in the console.
The English listing is addressed by the language key Amazon reports (`en-US`).

[Release index](README.md) · [Documentation index](../README.md).
