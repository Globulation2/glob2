# Qualify releases and publish downloads

Qualification gates and authoritative download manifests.

## Release qualification

Build success is a packaging check, not a release approval. Before a stable
publication, inspect the installed packages on actual desktop sessions, test
Flatpak and Snap on multiple distributions, check save continuity and replay/
network version gates, compare cross-platform simulation checksums for any
simulation changes, and play the release candidate. Retain the resulting files
and logs under `artifacts/` and attach review evidence to the release PR.


## Qualified downloads manifest

The all-platform launch gate requires Windows x64 installer and portable ZIP,
separate Intel and Apple Silicon DMGs, Linux x64 tarball/RPM/Flatpak/Snap,
and signed ARM64/ARMv7/x86-64 Android APKs. Google Play and iPhone/iPad App
Store listings must be publicly available in production. TestFlight, internal
tracks and successful CI builds do not satisfy these requirements. Supplemental
stores can be added after independently verifying their listings. Missing
publisher verification, signing credentials, real-device evidence or store
acceptance blocks launch; never fabricate an approval to unblock automation.

Signing, submission and publication run only from the release mirror; public
release assets remain on `Globulation2/glob2`. Windows uses Azure Artifact
Signing public trust with mirror/environment-bound GitHub OIDC, publisher
identity verification, and timestamped signatures. Direct Mac DMGs use a
Developer ID Application identity, hardened runtime, notarization and stapling,
separate from Mac App Store credentials. Android sideload packages use a
permanent backed-up key separate from development and Play upload keys. Keep
signing material and private qualification records in protected mirror
environments or external secret storage, never Git or public artifacts.
Android channels with different keys cannot update one another: back up saves,
uninstall, then install the other channel. Preserve application identities and
save paths across releases.

Configure protected mirror environments before dispatch: `windows-signing`
requires Azure signing endpoint/account/profile, verified publisher identity and
GitHub OIDC restricted to the mirror signing environment; `macos-developer-id`
requires the Developer ID certificate identity and notarization authentication,
separate from App Store signing; `android-sideload` requires the permanent key,
passwords, alias and pinned certificate SHA-256. Configuration alone does not
establish installed-package qualification or public mobile store acceptance.

For staging, dispatch `github-release.yml` from mirror `master` as the owner
with the existing public tag. For public visibility, dispatch
`promote-downloads.yml` with that same `tag`, an HTTPS `qualification_url`
pointing to the reviewed JSON, and its exact `qualification_sha256`. The
protected `github-release` environment gates the publication token. Promotion
checks the tagged checkout, downloads the draft packages/inventory, verifies the
reviewed JSON digest and generates the manifest from final bytes before setting
`--draft=false`. Interrupted promotion retries reuse identical metadata and
reject changed metadata; they do not replace stable package bytes. Keep private
evidence retrieval URLs free of embedded credentials; GitHub run inputs are
visible to people with repository access.

`tools/release/downloads_manifest.py` generates `downloads-manifest.json` and an
optional checksum inventory from the **final** signed/notarized file bytes. It
requires the checkout HEAD, local immutable version tag and source commit to
match, and the tag to match `PACKAGE_VERSION`. The publication workflow must
also fetch and verify the tag against public upstream before running this
command. Do not move tags or replace files attached to a published stable tag.

```sh
python3 tools/release/downloads_manifest.py \
  --artifacts artifacts/release-final \
  --inventory artifacts/release-inventory.json \
  --qualification artifacts/release-qualification.json \
  --tag vVERSION --source-commit FULL_PUBLIC_COMMIT_SHA \
  --output artifacts/release-final/downloads-manifest.json \
  --checksums artifacts/release-final/SHA256SUMS
```

The inventory is a JSON object with `schemaVersion: 1`, `sourceCommit`,
`packages` and `sources`. Each package explicitly declares `platform`,
`architecture`, `format`, `minimumOs` and `filename`; Linux packages may include
`dependencies`, an array of installation prerequisites. Filenames are plain
basenames, not paths. Supported identities are Windows `x86_64` with `exe` and
`zip`; macOS `arm64` and `x86_64` with `dmg`; Linux `x86_64` with `tar.gz`,
`rpm`, `flatpak` and `snap`; Android `arm64`, `armv7` and `x86_64` with `apk`.
At least one source entry is required; each declares `filename` and must remain
distinct from playable packages. Set minimum OS and dependencies from actual
build/installed-package qualification, not the runner's name alone. Mac launch builds target macOS 15+;
Linux tarballs document their tested Ubuntu baseline and RPMs their target
Fedora version.

The private qualification JSON uses `schemaVersion: 1`, `tag`, `sourceCommit`,
a named `reviewedBy`, a past timezone-qualified ISO `reviewedAt`, and an HTTPS
`evidenceUrl` accessible to reviewers. Its `artifacts` object maps every package
and source filename to its final SHA-256, with no omitted or extra files. Its
`platforms` object requires literal true assertions for Windows `signed` and
`tested`; macOS `signed`, `notarized` and `tested`; Linux `tested`; Android
`signed` and `tested`; and iOS `tested`. Its `compatibility` object requires
`simulationChecksumsMatch`, `saveLoad`, `replayNetwork` and `onlinePlay`, each
literal true. Its `testedTargets` object requires literal true entries for
`windows-x86_64`, `macos-arm64`, `macos-x86_64`, `linux-flatpak`, `linux-snap`,
`linux-tar.gz`, `linux-rpm`, `android-arm64`, `android-armv7`, `android-x86_64`,
`ios-iphone` and `ios-ipad`. Each target attests to installation and gameplay
qualification of that package/architecture/device, not merely a successful
build. Set these only after review of evidence identifying the selected commit, artifact digests, devices, OS/toolchains, commands and limits.

The `stores` object requires both `googlePlay` and `appStore` entries, each
containing a verified HTTPS production listing `url` and `production: true`.
The Google Play listing must identify `org.globulation2.glob2`.
These are reviewed availability attestations, not automatic store API checks.
Review clean install/update, tutorial/full match, audio, saves, editor, online
play and mobile lifecycle/input on clean desktops, both Mac architectures,
representative Linux distributions, Android ARM devices and x86-64 emulator,
and an iPhone and iPad. Compare simulation checksums across architectures and
exercise the existing save, replay and network acceptance gates. Retain symbols,
logs, screenshots and qualification records outside source control.

The public manifest contains `schemaVersion`, `version`, `tag`, `sourceCommit`,
`releaseNotesUrl`, `qualification`, `packages` and `sources`. Package descriptors
receive final `sizeBytes`, `sha256` and immutable release `url`. Source entries
receive the same file metadata separately. Public qualification contains only
`qualified: true`, the matching `sourceCommit`, and production store URLs; it
omits private reviewer/evidence metadata. Validation failures preserve the
previous manifest file. Publish this manifest only after every gate passes;
a reviewed website metadata PR then imports it without filename guessing or
runtime GitHub requests. Stage packages as a draft while awaiting store review.

[Release index](README.md) · [Documentation index](../README.md).
