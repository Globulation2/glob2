# Release packaging

The public `.github/workflows/release.yml` builds packages without publishing.
The public desktop publication workflows are mirrored to the owner-controlled
`genixpro/glob2-release` repository. Configure release credentials as restricted
GitHub environment secrets there; the repository itself is public.
Run `publish-desktop.yml` manually with the public `vVERSION` tag to publish the
GitHub release, publish Snap stable, and propose the Flathub update in that
order. Each channel retains a separate manual workflow for retries; no
publication workflow runs on ordinary pushes or pull requests.
Each publication workflow fails before building packages unless it is manually
dispatched from the release repository's `master` branch by the owner account;
the same check applies to a re-run's initiator. It then
checks its required release-repository secret (and the Flathub fork variable)
and fails if any are missing. The public `release.yml` remains a build-only
workflow and does not use publication credentials.
The selected public `vVERSION` tag must resolve to the release mirror's HEAD.
Mirror only reviewed public commits. Review of workflow changes is essential:
the mirror and dispatch gates alone do not make unreviewed code safe to run.
Before tagging a new release, choose an unused version, update
`PACKAGE_VERSION` in `scons/build_layout.py`, `vcpkg.json`, and
`fedora/glob2.spec`, and add its AppStream release notes. Do not move an
existing tag. `tools/release/release.py check --tag` requires the tag and the
release mirror's HEAD to identify the same commit.
Build-only runs leave Flatpak off by default while its first listing metadata is
being prepared; select `build_flatpak` after adding AppStream release notes to
exercise that recipe. Flathub's linter rejects the current metadata until those
notes exist. The GitHub
publication run builds it.
Publication also requires matching AppStream release notes and a hosted gameplay
screenshot. A native Linux capture of the active menu colony is in
`data/screenshots/`; its commit-pinned URL is in the AppStream metadata. Keep
that commit reachable and confirm the URL resolves before publication. Add the
release notes once the release candidate is settled. The
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
build and install a candidate, and set the `snap-release` environment secret
`SNAPCRAFT_STORE_CREDENTIALS` from a restricted `snapcraft export-login` token
with package access, push, update and release rights for that name. The
`snap-release.yml` workflow publishes only Snap stable. Configure the
`PUBLIC_RELEASE_GH_TOKEN` secret in the `github-release` environment
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

The mobile and release workflows build unsigned APKs and retain them only as
validation artifacts. They are not GitHub release assets and cannot be installed
as public releases. A temporary developer signature supports emulator and
device checks. F-Droid's signature is the sole public F-Droid update channel.
Play and F-Droid use different signing keys, so Android cannot update an
installation from one store with an APK from the other. A store switch requires
uninstalling the installed copy; export or back up saves first.
Before tagging, test all three builds with `fdroid lint` and `fdroid checkupdates`.
For the `fdroid build --server` trial, use a disposable copy of the recipe whose
three `commit` fields point to the reviewed candidate commit SHA; the public tag
does not exist until these gates pass. Restore the tag references before submitting
the recipe to `fdroiddata`. Play the candidate on real ARM64 and 32-bit ARM
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
