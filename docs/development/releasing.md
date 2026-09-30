# Release packaging

The public `.github/workflows/release.yml` builds packages without publishing.
The public Epic, GitHub, Snap, and Flathub workflow definitions are mirrored to
the owner-controlled private `genixpro/glob2-release` repository, which stores
release credentials. Each channel has a separate manual `workflow_dispatch` run.
The selected public `vVERSION` tag must resolve to the private mirror's HEAD.
Mirror only reviewed public commits. Review of workflow changes is essential:
the private repository alone does not make unreviewed code safe to run.
Build-only runs leave Flatpak off by default while its first listing metadata is
being prepared; select `build_flatpak` to exercise that recipe. The GitHub
publication run builds it.
Publication also requires matching AppStream release notes and a hosted gameplay
screenshot. A native Linux capture of the active menu colony is in
`data/screenshots/` and its
version-tagged URL is in the AppStream metadata; confirm that URL resolves after
tagging. Add the release notes once the release candidate is settled. The
256x256 application icon is derived from the existing desktop artwork and is
installed with other icon sizes.
Flathub reads the AppStream screenshot from the installed metainfo file. The
Snap Store manages screenshots in its listing separately from `snapcraft.yaml`;
upload `data/screenshots/globulation2-gameplay.png` to that listing when the snap
name is registered. Snapcraft imports the same AppStream component ID for the
app's desktop metadata, but `snapcraft upload-metadata` transfers summary,
description and icon only.

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
data, maps, campaigns and scripts. Build-only runs can be started from a branch;
publication requires the exact version tag on the checked-out commit. The
source checksum on a build-only run is for testing, not a release announcement.

## Store setup

Before publishing a Snap, register the `globulation2` name in the Snap Store,
build and install a candidate, and set the private `snap-release` environment secret
`SNAPCRAFT_STORE_CREDENTIALS` from a restricted `snapcraft export-login` token
with package access, push, update and release rights for that name. The
`snap-release.yml` workflow publishes only Snap stable. Configure the
`PUBLIC_RELEASE_GH_TOKEN` secret in the private `github-release` environment
with permission to create releases in `Globulation2/glob2`. The
`github-release.yml` workflow publishes the GitHub packages independently.

The Flatpak recipe in `flatpak/org.globulation2.Globulation2.yml.in` pins every
third-party source and is rendered with the selected commit. Once its package
and metadata pass Flathub's build and review requirements, submit the rendered
manifest to Flathub under `org.globulation2.Globulation2`. Flathub publishes
from its own manifest repository after its review; a GitHub release cannot
directly publish to Flathub. Once the Flathub app repository exists, create a
writable fork and set `FLATHUB_FORK` (an `owner/repository` GitHub Actions
variable) and `FLATHUB_GH_TOKEN` (a secret with permission to push that fork and
open a PR). `flathub-update.yml` then opens a Flathub update PR. A Flathub maintainer must
review and merge it; the workflow does not bypass that review. Keep the
upstream AppStream metadata in `data/` updated with each release's notes and
screenshots.

## Epic Games Store

The publisher signs in to the [Epic Developer Portal](https://dev.epicgames.com/portal),
creates its organization and Globulation 2 product, accepts the distribution
agreement, completes business and payout details, and pays the submission fee.
Configure a free base offer, product page, ratings, support details and a Windows
artifact. Obtain the dedicated BuildPatchTool client ID and secret in Product
Settings; these are separate from Epic Online Services credentials.

In the private repository's `epic-dev` environment, set variables
`EPIC_ORGANIZATION_ID`, `EPIC_PRODUCT_ID`, `EPIC_WINDOWS_ARTIFACT_ID`, and
`EPIC_BPT_CLIENT_ID`, plus secret `EPIC_BPT_CLIENT_SECRET`. Download the current
Windows BuildPatchTool ZIP from the product's Artifacts and Binaries page. Upload
it as `BuildPatchTool.zip` to a private `epic-bpt` release in
`genixpro/glob2-release` and set `EPIC_BPT_SHA256` to the ZIP's SHA-256 in the
same environment. Review tool updates before replacing the asset and digest.

`epic-windows-release.yml` builds a Windows install directory, hashes every file,
smoke-tests the artifact on a separate Windows runner, verifies the hashes again,
then uploads the candidate to Epic **Dev**. The credential-bearing job does not
execute game or source-tree scripts. Retain the workflow's manifest and log as
release evidence. The unique Epic build version contains the source commit and
workflow run number.

Install the candidate through the Epic launcher and test launch, assets, saves,
and multiplayer with a non-Epic PC build. Use the Developer Portal to move it
through Stage, review, and Live as required. BuildPatchTool upload alone does not
publish a listing; Epic's self-service documentation does not describe a
supported API for portal promotion. Add macOS after validating its `.app` through
the same launcher and gameplay checks. Linux continues through other channels.
Epic requires crossplay between PC storefronts for multiplayer games; the
existing YOG/LAN implementation may satisfy this only after an actual mixed-store
test. Check whether any other PC store offers Globulation 2 achievements before
marking achievements unnecessary on Epic.

## Distribution maintainers

### F-Droid Android release

The main F-Droid repository builds and signs `org.globulation.glob2` from a
public `vVERSION` tag. The upstream build recipe is
`fdroid/metadata/org.globulation.glob2.yml`; submit its tested contents to
F-Droid's `fdroiddata` project. `mobile/android-release.json` gives the version
name and base code, and the `armeabi-v7a`, `arm64-v8a`, and `x86_64` APKs use
`10 * base + 1`, `+ 2`, and `+ 3`, respectively. The `fastlane/metadata/android/`
tree contains the listing text, icon, per-APK changelogs, and Android screenshots.
Release preflight checks all three recipe codes and rejects missing listing
material. Do not use a
desktop or browser screenshot as an Android screenshot.

The mobile and release workflows build unsigned APKs and retain them only as
validation artifacts. They are not GitHub release assets and cannot be installed
as public releases. A temporary developer signature supports emulator and
device checks. F-Droid's signature is the sole public Android update channel.
Before tagging, test all three builds with `fdroid lint`, `fdroid checkupdates`,
and `fdroid build --server`, then play the candidate on real ARM64 and 32-bit ARM
devices and an x86_64 emulator. Record APK digests, logs, screenshots, save/load,
rotation, lifecycle, keyboard, touch, and editor results under `artifacts/` for
review. For each ABI, run `python3 mobile/compare_fdroid_apks.py --arch ABI
--github-apk GITHUB_APK --fdroid-apk FDROID_APK` and review any native library
differences against the retained symbol/build records. The daily
`fdroid-publication.yml` workflow checks F-Droid's package API
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
