# Desktop publication

Publish native release packages and update their download metadata.


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

[Release index](README.md) · [Documentation index](../README.md).

## Steam Windows depot source

Dispatch `steam-windows-package.yml` or `steam-windows-upload.yml` with a
required immutable public `tag`. Reusable packaging callers also pass `tag`.
The workflow resolves that tag in the public repository and pins both the
build and smoke fixture checkouts to its commit. Depot artifacts use the
resolved source SHA, include a checksummed `source-commit.txt`, and the upload
job verifies that identity before SteamPipe runs. The upload creates an
unpublished build; it does not promote a live Steam branch.
