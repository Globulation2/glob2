# Release packaging

## The release mirror

Release pipelines run only in the release mirror, the owner-controlled public
`genixpro/glob2-release` repository. Every release, publication and deployment
workflow is defined here, in `Globulation2/glob2`, but each of its root jobs
has a job-level `if:` that requires the mirror's name and numeric repository ID
(`1397722696`), plus the owner and the expected ref. Anywhere else, including
this repository and forks, every job is skipped before a runner starts. None of
these workflows has a pull request trigger.
`test/build_system/test_release_guards.py` enforces this for every workflow in
the release set in `test/build_system/test_ci_concurrency.py`, and fails if a
workflow that uses deployment environments, named secrets or write tokens is
missing from that set. Add a new release workflow to that set and give its root
jobs the same guard.

The mirror's `master` has exactly the same files as `Globulation2/glob2`
`master`: each sync is a merge commit whose tree is upstream's, and the mirror
carries no changes of its own. The mirror's older history has its own merge
commits, so syncs cannot be fast-forwards, and its `master` refuses force
pushes. A change needed
for releasing, including a workflow or packaging fix, is made here first,
through a normal pull request, and reaches the mirror with the next sync.
Configure release credentials only as restricted GitHub environment secrets in
the mirror.

## Desktop publication

The `.github/workflows/release.yml` package build does not publish; the
publication workflows below call it.
Run `github-release.yml` manually with the existing public `vVERSION` tag to
preflight protected mirror configuration, build from its exact source commit,
sign direct-download packages, submit an internal Play candidate and an
App Store-eligible external TestFlight candidate, and stage a **draft** GitHub
release. `publish-desktop.yml` calls this staging workflow before the optional
Snap stable and Flathub submission workflows. Neither workflow makes the GitHub
release public; only `promote-downloads.yml` can do that after qualification.
Each channel retains a separate manual workflow for retries; no publication
workflow runs on ordinary pushes or pull requests.
Each publication workflow skips every job unless it is manually dispatched
from the release repository's `master` branch by the owner account; the same
check applies to a re-run's initiator. It then
checks its required release-repository secret (and the Flathub fork variable)
and fails if any are missing. `release.yml` is a build-only workflow and does
not use publication credentials.
The release repository permits only an explicit list of SHA-pinned actions.
When changing a release workflow's actions, pin each action to a reviewed
commit and add only that exact reference to the release repository's allowed
actions list before running it there.
The selected public `vVERSION` tag supplies the game source for every
publication build, including Android and iOS submissions. The release mirror's
`master` supplies the reviewed workflow and protected environment configuration;
its HEAD can be newer than the public tag. Publication uses
`tools/release/release.py check --tag` to verify the checked-out public source
and tag identify the same commit; browser publication checks the tag commit and
game version directly. Mirror only reviewed public commits. Review of workflow changes is essential:
the mirror and dispatch gates alone do not make unreviewed code safe to run.
Before tagging a new release, choose an unused version, update
`PACKAGE_VERSION` in `scons/build_layout.py`, `vcpkg.json`, and
`fedora/glob2.spec`, and add its AppStream release notes. Do not move an
existing tag.
Build-only runs leave Flatpak off by default because it takes longer to build;
select `build_flatpak` to exercise that recipe. An untagged run builds its other
packages from the mirror checkout and pins the Flatpak recipe to the latest
public source commit contained in that checkout. Tagged publication builds pin
the selected public tag. The GitHub publication run builds Flatpak.
Publication also requires matching AppStream release notes and a hosted gameplay
screenshot. A native Linux capture of the active menu colony is in
`data/screenshots/`; the AppStream metadata pins its URL to the commit that
introduced it so Flathub can mirror a stable image. Confirm the URL resolves
before publication, and pin any replacement screenshot to its own commit. Add
the release notes once the release candidate is settled. The
256x256 application icon is derived from the existing desktop artwork and is
installed with other icon sizes.
Flathub reads the AppStream screenshot from the installed metainfo file. The
Snap Store manages screenshots in its listing separately from `snapcraft.yaml`;
upload `data/screenshots/globulation2-gameplay.png` to that listing when the snap
name is registered. Snapcraft imports the same AppStream component ID for the
app's desktop metadata, but `snapcraft upload-metadata` transfers summary,
description and icon only.

## Browser publication

Dispatch `browser-release.yml` in `genixpro/glob2-release` from `master` with
an existing public `vVERSION` tag. Only an owner manual dispatch (including the
initiator of a rerun) passes the context gate. This workflow is separate from
the desktop release orchestrator and has no push, pull-request or reusable
workflow trigger.

The build job verifies the exact public tag commit and matching game version
(without requiring desktop store metadata), installs the locked
Emscripten SDK, builds `scons target=web release=1`, and packages the client with
`browser/package-static.py`. Its `browser-release` artifact includes SHA-256
checksums. The isolated publication job verifies the artifact before using
Google Cloud Workload Identity Federation through the `browser-release`
GitHub environment; the build job has no cloud identity permission.

The destination is `gs://glob2-browser-pharaoh-418820-20260909` in project
`pharaoh-418820`. Versioned JavaScript, WebAssembly and data objects receive
explicit content types and immutable one-year caching. They are uploaded before
`index.html`, which requires cache revalidation. Older assets remain available
for open sessions and rollback; publication never synchronizes with deletion.
The public entry point is
<https://storage.googleapis.com/glob2-browser-pharaoh-418820-20260909/index.html>.

The release environment requires `GLOB2_BROWSER_WIF_PROVIDER` and
`GLOB2_BROWSER_SERVICE_ACCOUNT` variables. The dedicated `glob2-browser-release`
provider restricts tokens by repository ID, owner actor ID, manual event,
`master`, public visibility, hosted runner, environment and exact workflow path.
The service-account federation binding uses the provider-specific
`attribute.browser_release_repository_id/1397722696` principal set. Only this
provider maps that attribute; retain its workflow and environment restrictions
and do not map the attribute in unrelated providers. This also avoids dependence
on GitHub's mutable versus immutable subject string format.
Its service account has `roles/storage.objectUser` on this bucket only, with no
project-wide storage role or persistent service-account key. Add the pinned
`google-github-actions/setup-gcloud` reference from the workflow to the mirror's
allowed actions, preserving its existing list. Configure the environment to
allow only `master`. Mirror reviewed workflow changes before dispatching; an
implementation change alone does not publish or replace the live browser.

## Outputs

The workflow produces a reproducible source archive and SHA-256 checksum, an
Ubuntu 22.04-built Linux installation archive, a Windows x86-64 ZIP with its
MinGW DLLs, a macOS DMG, Fedora source and binary RPMs, a Flatpak bundle and
pinned build manifest, and a Snap.
It also compiles the source on Debian trixie, Fedora 43, Arch Linux and openSUSE
Tumbleweed. These extra builds check portability; they do not upload into those
distributions' official repositories. The Ubuntu-built archive relies on system
libraries and is labelled for its build baseline. Flatpak and Snap are the
cross-distribution packages.

The source archive is made from the selected Git commit, not uncommitted files.
The release script checks that it contains the build files and essential assets.
`.gitattributes` omits the historical `debian/` directory from this archive so
Debian's maintained packaging can supply its own files.
Installed packages are checked for the executable, desktop integration, game
data, maps, campaigns and scripts. Build-only runs (no tag) are dispatched from
the mirror's `master` like every release workflow; publication requires the exact version tag on the checked-out commit. The
source checksum on a build-only run is for testing, not a release announcement.

## Store setup

### Epic Games Store

The first Epic artifact is Windows. Create a free base offer and a Windows
artifact in the Epic Developer Portal. Finish the Dev listing with product and
offer images, at least one 1920x1080 image from the actual game in the media
carousel, accurate Windows requirements, support details, and regions and
ratings. Keep illustrated key art distinct from gameplay screenshots. Confirm
trader, tax, and payout onboarding in the portal before requesting review.

For each candidate, create a public `vVERSION` tag and mirror the reviewed
Epic workflow to the release repository's protected `master` branch. Dispatch
`epic-windows-release.yml` there with the tag. Its preflight checks the public
tag and version, then records the exact public source commit. The packaging
and smoke-test jobs check out that commit and have no Epic credentials. The
upload job verifies every staged file against a
SHA-256 manifest before it receives the BPT secret through the `epic-dev`
environment. The workflow uploads to Epic **Dev** only. In the portal's
Artifacts and Binaries page, open the Windows artifact and make the new binary
active for Windows; newly uploaded binaries are inactive and scheduled for
deletion until activated. Install and test that active Dev build through the
Epic launcher, then move the candidate through Stage and review, and promote
it to Live in the portal.

In the release repository, set `EPIC_ORGANIZATION_ID`, `EPIC_PRODUCT_ID`,
`EPIC_WINDOWS_ARTIFACT_ID`, `EPIC_BPT_CLIENT_ID`, and `EPIC_BPT_SHA256` as
`epic-dev` environment variables. Store `EPIC_BPT_CLIENT_SECRET` as an
environment secret. Create those BPT credentials from the product's BPT
Credentials page; EOS credentials are different. Download the current
BuildPatchTool from the product's Epic Artifacts and Binaries page and set
`EPIC_BPT_SHA256` to the ZIP's SHA-256 digest. The upload job downloads from
Epic's official endpoint and checks this digest before use. An Epic tool
update intentionally stops the job until its digest is reviewed and refreshed.
Never put BPT credentials in the public source tree or GitHub Actions
variables. The release repository is public, but its `epic-dev` environment
secret is masked and available only to jobs explicitly using that environment.
If the client secret is ever stored as a variable, delete that variable and
rotate the BPT client before uploading. The Epic workflow rejects a
secret-named variable before use. Review changes to the release workflow and
environment access before dispatching an upload.

After each upload, retain the workflow's staged manifest, version, source
commit, and BPT log. Install through Epic on a fresh Windows machine and check
startup, assets, campaign and save continuity, and multiplayer against a
non-Epic PC build. If simulation behavior changed, compare replay acceptance
and per-tick checksums across platforms. Repeat this process with a new build
version for updates; do not reuse a previous Epic artifact version.

### Snap, GitHub, and Flathub

Before publishing a Snap, register the `globulation2` name in the Snap Store,
build and install a candidate, and set the `snap-release` environment secret
`SNAPCRAFT_STORE_CREDENTIALS` from a restricted `snapcraft export-login` token
with package access, push, update and release rights for that name. The
`snap-release.yml` workflow publishes only Snap stable. Configure the
`PUBLIC_RELEASE_GH_TOKEN` secret in the `github-release` environment
with permission to create releases in `Globulation2/glob2`. The
`github-release.yml` workflow stages final packages and `package-inventory.json`
in a draft GitHub release. Its inventory normalizes Snap/RPM filenames and
includes all eleven playable packages plus the tagged source archive.
`promote-downloads.yml` independently downloads and validates these bytes before
publication; see the qualified manifest procedure below.

The Flatpak recipe in `flatpak/org.globulation2.Globulation2.yml.in` pins every
third-party source and is rendered with the selected commit. Once its package
and metadata pass Flathub's build and lint checks, fork `flathub/flathub` with
its `new-pr` branch, add the rendered manifest at the fork's top level, and
submit a first-listing PR targeting `new-pr` under
`org.globulation2.Globulation2`. Do this only for a selected stable release
candidate: acceptance creates the public listing. Flathub publishes from its
own manifest repository after its review; a GitHub release cannot directly
publish to Flathub. Once the Flathub app repository exists, create a
writable fork and set `FLATHUB_FORK` (an `owner/repository` GitHub Actions
variable) and `FLATHUB_GH_TOKEN` (a secret with permission to push that fork and
open a PR). `flathub-update.yml` then opens a Flathub update PR. A Flathub
maintainer must review and merge it; the workflow does not bypass that review.
Keep the upstream AppStream metadata in `data/` updated with each release's
notes and screenshots.

## Distribution maintainers

### F-Droid Android release

The main F-Droid repository builds and signs `org.globulation2.glob2` from a
public `vVERSION` tag. This is the same application ID used by Play. The
`--fdroid` build mode applies F-Droid's version codes and unsigned APK checks.
The upstream build recipe is `fdroid/metadata/org.globulation2.glob2.yml`;
submit its tested contents to F-Droid's `fdroiddata` project.
`mobile/android-release.json` gives the version
name and base code, and the `armeabi-v7a`, `arm64-v8a`, and `x86_64` APKs use
`10 * base + 1`, `+ 2`, and `+ 3`, respectively. The `fastlane/metadata/android/`
tree contains the listing text, icon, per-APK changelogs, and Android screenshots.
Release preflight checks all three recipe codes and rejects missing listing
material. Do not use a
desktop or browser screenshot as an Android screenshot.

The mobile and build-only release workflows retain unsigned APKs as validation
artifacts; those files are not installable public releases. A temporary
developer signature supports emulator and device checks. The all-platform
staging workflow signs those verified unsigned APKs in its protected
`android-sideload` job using `mobile/sideload_release.py`, verifies the pinned
permanent signing certificate and alignment, and stages only the final signed
APKs. F-Droid's signature remains the sole public F-Droid update channel.
The reviewed `.github/workflows/fdroid-release-validation.yml` workflow is
mirrored to `genixpro/glob2-release` and runs by owner dispatch with
the exact public candidate commit SHA. Its read-only jobs build all three
unsigned APKs through
`mobile/fdroid_build.py`, retain checksums and logs, and launch the x86_64
candidate in an emulator. After approval and tagging, dispatch it again with
the public tag to verify the tag points to that same source commit. Compare its
APKs with the public workflow artifacts before submitting the recipe.
Play and F-Droid use different signing keys, so Android cannot update an
installation from one store with an APK from the other. A store switch requires
uninstalling the installed copy; export or back up saves first.
Before tagging, run `fdroid lint` and exercise `fdroid checkupdates` with a
disposable tag fixture; the update checker cannot discover an unpublished
public tag. Confirm it generates all three ABI version codes.
Dispatch `F-Droid buildserver recipe trial` on private `genixpro/glob2-release`
for each ABI with the exact public candidate commit SHA. It runs the recipe with
`fdroid build --on-server` inside F-Droid's pinned buildserver image. The trial
uses a disposable copy of the recipe whose three `commit` fields point to that
SHA; the public tag does not exist until these gates pass. The source build
installs the repository's checksum-pinned JDK so its Java bytecode can be
compared with GitHub's APK. Retain the trial APKs and logs, and restore the tag
references before submitting the recipe to `fdroiddata`. Play the candidate on
real ARM64 and 32-bit ARM devices and an x86_64 emulator. Record APK digests,
logs, screenshots, save/load,
rotation, lifecycle, keyboard, touch, and editor results under `artifacts/` for
review. For each ABI, run `python3 mobile/compare_fdroid_apks.py --arch ABI
--github-apk GITHUB_APK --fdroid-apk FDROID_APK` and review any native library
differences against the retained symbol/build records. The daily
`fdroid-publication.yml` workflow in the release mirror checks F-Droid's package API
for all three codes after the GitHub release and opens one tracking issue when
publication remains incomplete after 72 hours.

F-Droid's initial listing requires an accepted `fdroiddata` merge request.
Subsequent tagged updates use F-Droid's update checker; GitHub Actions does not
publish directly to the main F-Droid repository. Review the shipped asset
licenses against `docs/assets/source-attribution.md` and the font licenses with
the first submission.

The Fedora spec in `fedora/glob2.spec` is a starting recipe for the new source
archive. The checked-in `debian/` directory is historic and is **not** used by
this workflow; work with the Debian Games Team's Salsa package before proposing
an update. Debian's upload can subsequently reach Ubuntu during its development
cycle. Arch and openSUSE have their own maintainers and package repositories.
Send each maintainer the source archive, checksum, dependency changes, release
notes, build results, and any save/replay/network compatibility notes. Official
repository uploads require their review and cannot be performed by our GitHub
token.

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


## iOS production submission and manual publication

Dispatch `ios-testflight.yml` from mirror `master` as the owner with the exact
public tag and `audience: external`. An eligible upload emits the
`ios-upload-provenance` artifact only after upload succeeds, recording the tag,
source commit, application ID, marketing version, build number and archive binary
hash. The artifact is retained for 90 days. Save release evidence externally if
review may outlast that retention period. Internal-only uploads cannot be used
for App Store production.

Dispatch `ios-production.yml` with the same `tag`, the successful mirror
`upload_run_id`, and one deliberate `stage`: `prepare`, `submit` or `publish`.
The context job verifies the owner-dispatched mirror run, expected workflow,
success and exact source provenance before the protected `ios-testflight`
environment supplies App Store Connect credentials. Apple queries must find
exactly one processed, eligible build with that build number and marketing
version. A version attached to a different build is never overwritten.

`prepare` creates or binds the selected version with `MANUAL` release and does
not submit it. Complete private store listing configuration before `submit`:
copyright, localized descriptions and HTTPS support links, primary-locale
privacy policy, fully uploaded iPhone and iPad screenshots, and review contact
information plus demo credentials when required. Apple validates additional
agreements, ratings and review requirements; failures remain blocking.
`submit` requests Apple review without public distribution. Only `publish`,
after Apple returns `PENDING_DEVELOPER_RELEASE` for the same manually released
version and build, requests public distribution. An accepted upload or pending
review never counts as production availability for the website launch gate.

`mobile/ios_release.py` encodes the four-component game version `a.b.c.d` as
App Store marketing version `a.b.(100*c+d)`. Each component is numeric without
leading zeroes and `d` must be below 100 to avoid collisions. Unsupported versions
fail before the iOS build; the generated Info.plist and provenance use this exact
encoding instead of a stale fixed marketing version.

Run the focused metadata contracts with:

```sh
python3 -m unittest discover -s test/build_system -p test_downloads_manifest.py -v
```
