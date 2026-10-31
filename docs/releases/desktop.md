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
`macos-signing-retry.yml` can retry direct Mac signing without rebuilding. Pass
its immutable public tag and the owner-dispatched all-platform source run ID.
It verifies every successful source-context attempt, both successful native Mac
package jobs, and each artifact's GitHub SHA-256 digest before importing signing
credentials. The source run may have failed at signing. Qualification requires the latest attempt of each signing job to succeed; earlier failed signing attempts do not invalidate a recovered run. Source context and package build attempts must still all succeed. Unexpected contents,
expired artifacts and mixed revisions fail closed. The mirror's reviewed driver
imports the pinned Apple Developer ID G2 intermediate and adds the isolated
keychain to the search list; the selected game source still supplies the app and
its signing checks. The retry uploads only notarized final DMGs and never
publishes a release. Retain the retry run ID with qualification evidence.
After both signed Mac artifacts finish in an all-platform run, dispatch
`macos-downloads-qualification.yml` with its tag and run ID. It checks source-run
provenance and final artifact digests, verifies notarization, Gatekeeper,
architecture and minimum OS, and installs the final bytes outside the checkout
on native Apple Silicon and Intel runners. Both execute the frozen
`test/fixtures/releases/qualification-river.map.gz` fixture with game seed 713,
create a one-tick baseline save in the current format, repeat continuation to
600, save at 300 and resume to 600, then finish a full match and
create a replay. The comparison job rejects any per-tick divergence.
Deep-world traces and snapshot-v3 traces are checked separately at every tick;
result endpoints are matched to their declared checksum algorithm. Save/resume
and repeatability must agree in both formats when snapshot traces are present.
The comparison also requires matching release, CLI, save, network and SDL
versions. Each architecture retains its own compilation timestamp in its report;
that timestamp is not a compatibility identity. Configure
`MACOS_DOWNLOAD_TEAM_ID` only in the mirror's Actions variables; qualification
jobs use no signing or publication credentials. Reports and traces remain CI
artifacts and include package digests, source SHA and verifier/run identities.
Qualification artifacts retain command diagnostics, saves, result JSON and tick
traces even when a native check fails, so failures can be investigated against
the exact signed package without rebuilding it.
Interactive tutorial, audio, editor, updates and online play still require
separate qualification; this workflow does not publish or deploy anything.
Android emulator checks use the reviewed mirror smoke driver to complete audio and hand-preference setup through real input before testing the main menu and lifecycle. The APK build and its assets still come from the immutable public tag.
When restarting all-platform staging after the selected tag has already reached
Google Play internal testing, verify that successful upload job and select
`play_preflight_only` on `github-release.yml` to check Play access without trying
to upload the same version code again. Retain the original upload run as release
evidence; this option does not attest to a new upload or production availability.
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
Mac package jobs pin Python 3.14.8 and SCons 4.11.1 and set `PYTHONPATH` to the
checkout so spawned asset encoder workers can import the source helpers.

Build-only runs leave Flatpak off by default because it takes longer to build;
select `build_flatpak` to exercise that recipe. An untagged run builds its other
packages from the mirror checkout and pins the Flatpak recipe to the latest
public source commit contained in that checkout. Tagged publication builds pin
the selected public tag. The GitHub publication run builds Flatpak. The Flatpak recipe comes from the
reviewed mirror workflow revision and pins its game source to the selected public
commit. Its build exposes the source directory on `PYTHONPATH` so spawned asset
encoder workers can import the tagged tools under SCons.
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

Windows installer upgrades retain the registered installation directory by
default, including legacy 32-bit registry entries. An explicit `/D=PATH` overrides
that location; put it last on the installer command line, without quoting the
path even when it contains spaces. Final signed-package checks exercise an
alternate explicit destination and a repair without `/D=`, in addition to
upgrade, uninstall and user-file preservation.

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

## Downstream Linux distributions

Official distro updates are a separate release deliverable. An upstream RPM or
successful distro compilation job does not update official repositories.
For each stable release, nominate a consenting upstream coordinator and open a
tracking issue recording the tag, source commit, archive SHA-256, downstream
versions, maintainers, submission URLs, blockers and feature omissions. Use
**preparing → submitted → accepted → available → installed-package verified**.
A merged recipe alone does not establish installed-package verification.

Prepare the handoff from the published source asset without replacing it:

```sh
python3 tools/release/downstream.py --tag vVERSION \
  --archive artifacts/glob2-VERSION.tar.gz --coordinator GITHUB_LOGIN \
  --output artifacts/downstream/VERSION
```

The tool checks the public stable release, resolves its tag to a commit and
verifies the archive's size, published SHA-256 and internal version. It writes
release identity, checksums and an initially unsubmitted campaign table, refusing
to overwrite existing progress. Keep changing status in the issue and generated
evidence in `artifacts/`, rather than committing campaign reports.

### Migration from 0.9.4.4

Use the release's source archive rather than legacy Savannah release discovery.
Review and retire old compiler, Python 2/SCons and SDL 1.2 patches. Current builds
use C++20, Python 3/SCons, SDL3 and its image/font/network libraries, OpenGL/epoxy,
Opus/opusfile/Ogg, Speex, Boost, OpenSSL, zlib and FriBidi. Consult the selected
release's build inputs for exact versions; the old SDL 1.2/Vorbis package list
is insufficient.

Leave `GLOB2_SDL3_PREFIX` unset for system SDL3. Review the release's SDL patches
(X11 window lifecycle, PNG channel preservation and font kerning) and test their
affected behavior before substituting unpatched libraries. Submit required fixes
to dependency maintainers. The pinned helper installs private SDL libraries for
upstream packages and does not establish distro unbundling.

Select [recording mode](../development/media-dependencies.md#linux-distribution-recording-variants)
explicitly. Distro packages may omit recording when policy or dependencies
require it; document this difference. Keep patched QuickJS/OpenLibm together:
their behavior is part of deterministic simulation, so arbitrary system
replacements are unsafe. The [vendored inventory](../../third_party/README.md)
and [licensing index](../development/licensing.md) identify code, asset, font,
soundtrack and editable-artwork notices. Audit disabled components still present
in the source archive; disabling recording does not remove its archived sources.

The pinned Pillow/libwebp asset encoder is a build dependency. Pre-provision it
with `tools/build_asset_encoder.py` and set `GLOB2_ASSET_ENCODER_PYTHON`;
developer builds may otherwise provision it automatically. Supply every source
generation input and build with networking disabled in a clean environment.
Game builds/installations work directly from the source archive. Evidence-producing
test harnesses require a Git checkout of the selected tag, with downstream patches
applied, so their embedded source provenance cannot borrow an enclosing repository
or mislabel archive contents as an unchanged commit.

Use `release=1 retain_symbols=1 client_profile=free`, distro build flags and
matching build/install options. Stage `BINDIR` and `INSTALLDIR`, keeping `DATADIR`
at the final location, usually `/usr/share`. Verify executable, data, campaigns,
maps, scripts, desktop metadata, icons, man page, completions and license notices.
Reject build-directory runtime library search paths.

Preserve profile/save locations and test upgrades from actual old distro
packages. Save loading follows the declared minimum format, while old replays
and mixed-client multiplayer have separate version gates: do not promise
compatibility with 0.9.4.4 peers. Include the release notes' AI, interface,
artwork, editor, online-platform and gameplay changes, with manual play evidence.

### Submission and verification

| Distribution | Packaging route |
| --- | --- |
| Debian | Start from [Games Team Salsa](https://salsa.debian.org/games-team/glob2), discovering its default branch. Preserve the executable/data split, add release watch metadata and propose an unstable upload. Follow testing migration. Do not revive upstream's historical `debian/` packaging. |
| Ubuntu | Follow Debian with a [sync or merge](https://documentation.ubuntu.com/project/how-ubuntu-is-made/processes/merges-and-syncs/) into the development series, accounting for deltas/freezes. Released-series backports require separate review. |
| Fedora | Work with the [maintainers](https://packages.fedoraproject.org/pkgs/glob2/glob2/) on dist-git, starting with Rawhide. Resolve bundled-library, codec and asset-source acceptance before supported-branch updates. |
| Arch | Update the [Extra package](https://archlinux.org/packages/extra/x86_64/glob2/) with a clean-chroot-tested PKGBUILD, avoiding duplicate AUR packages. |
| Gentoo | Check the current tree, removal history and bugs. Update or reintroduce with a consenting maintainer/proxy and tested keywords, following [submission guidance](https://devmanual.gentoo.org/ebuild-maintenance/new-ebuild/index.html). |
| openSUSE | Identify the OBS development package and maintainers; submit toward Factory/Tumbleweed. Evaluate Leap independently. |

Run native checks (lintian, RPM checks, namcap or pkgcheck), then qualify final
installed packages: clean install, upgrade, tutorial, audio, editor, saves,
online play and uninstall preserving user data. Compare identical seed/order
fixtures and per-tick checksums with upstream, including save/resume and
replay/network boundaries. Retain exact source/recipe revisions, environments,
commands, results, package hashes, features and untested architectures in
accessible evidence attached to the tracking issue.

Review open submissions weekly during the campaign, following distro escalation
rules. Configure Debian watch metadata and Fedora release monitoring for future
stable releases. Inventory NixOS, Alpine, Void, Mageia and other active packages
for a second wave. Existing Flatpak/Snap channels may bridge availability after
checking live listings. This process does not create upstream APT/RPM repositories
or guarantee downstream acceptance dates. If fixes require a new upstream source
release, publish a new patch version and explicitly update the campaign target;
never replace the selected release's source bytes.

## Steam Windows depot source

Dispatch `steam-windows-package.yml` or `steam-windows-upload.yml` with a
required immutable public `tag`. Reusable packaging callers also pass `tag`.
The workflow resolves that tag in the public repository and pins both the
build and smoke fixture checkouts to its commit. Depot artifacts use the
resolved source SHA, include a checksummed `source-commit.txt`, and the upload
job verifies that identity before SteamPipe runs. The upload creates an
unpublished build; it does not promote a live Steam branch.
