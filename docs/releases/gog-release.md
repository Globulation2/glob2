# GOG release candidates

Release workflow and verification requirements.

Globulation 2 is offered as a free, DRM-free Windows, macOS and Linux game. The
Galaxy SDK is not needed for the initial release. The public
`genixpro/glob2-release` mirror owns the manually triggered
`.github/workflows/gog-staging.yml` workflow. The main `Globulation2/glob2`
repository does not run GOG release jobs.

## Before GOG access

Submit the game through [GOG's submission form](https://lp.gog.com/submit-your-game/en).
The authorized project publisher must provide a contact, confirm rights to the
game and bundled third-party assets, and supply the free listing's key art,
description, screenshots, system requirements, supported languages and legal
lines. GOG provides the game product IDs, a Developer Portal account and access
to Pipeline Builder after onboarding. See GOG's
[essentials checklist](https://docs.gog.com/basic-game-assets/).

For the initial free GOG listing, confirm whether GOG accepts an Early Access /
beta designation before finalizing the store page. Describe what is playable
and what remains in development; do not use the game's historical release date
as evidence that the modernization effort is complete. See GOG's
[Early Access guidance](https://docs.gog.com/games-in-development/).

Follow the shared [release mirror policy](releasing.md#the-release-mirror).
Create a GitHub environment named `gog-staging`, restrict it to
`master`, and require the publisher's explicit approval for each upload. The
publisher may approve their own dispatch while operating alone. Put all
GOG secrets and variables in that environment, never in the game source or
workflow. The workflow permits only owner dispatch on the mirror's protected
`master` branch. Build and smoke-test jobs have read-only GitHub permissions
and no GOG credentials.

## Build a candidate

1. Publish an approved `vVERSION` tag in the main public repository. Mirror
   that tag's source into `genixpro/glob2-release`, then update its protected
   `master`. The preflight requires the tagged public commit to be an ancestor
   of the reviewed workflow driver. Build jobs check out that exact public
   commit separately and verify its identity and clean tracked tree. Packaging
   helpers run from the driver but read assets from the tagged checkout; smoke
   commands use the CLI version in that source. Unknown CLI versions fail
   closed. Release workflows and packaging helpers may be newer than the tag.
2. Manually start **GOG release candidate** in the mirror with the tag and
   `build` mode. It builds three depots, checks the complete file manifests,
   and runs headless game fixtures on all platforms. Each depot includes its
   game version, public source commit, workflow commit, SHA-256 file list and
   a `SOURCE.txt` link to the exact Corresponding Source archive.
3. Install the artifacts on clean Windows, macOS and Ubuntu machines. Launch
   the game offline without Galaxy, play a match, verify asset loading and save
   continuity, and inspect macOS Gatekeeper behavior. For simulation changes,
   compare the same seed and orders across platforms as described in
   [headless replay verification](../development/headless-replays.md). Keep evidence under
   `artifacts/` and attach reviewable files to the release PR.

All three depot builds reuse the game's checksum-pinned SDL3 dependencies.
Windows and Linux use the lean image runtime and the shared verified artwork
export; depot validation accepts its WebP images. Windows stages the private
SDL3 DLLs before the host runtime, and Linux resolves the installed executable's
library paths before copying its complete dependency closure.

The Linux depot includes a `start.sh` launcher, its game data and the linked
libraries that do not belong to the host graphics or base system. GOG asks for
a clean-machine launch without users installing missing dependencies; use its
[Linux guidance](https://docs.gog.com/linux-guidelines/) when qualifying each
release. The macOS depot contains `Glob2.app`, including scripts and bundled
library/license files. The Windows depot reuses the portable Windows packaging
helper. GOG's project template is `gog/project-template.json`; the renderer
fills one depot and one primary launch task per operating system.

## Reuse reviewed released payloads (build only)

Set `reuse_released_payload=true`, `mode=build`, `tag=v0.11.4.0`, and
`platforms=all` to prepare candidates from the already published Windows ZIP,
Ubuntu 22.04 tarball and two signed Mac DMGs. This route does not compile the
engine or regenerate artwork. The tag/source commit and each public asset's
SHA-256 are pinned in `tools/gog_release.py`; another release requires review of
its own pins. An unknown/moved tag, mismatched version or modified asset fails
before staging. The default `false` retains the source-build workflow above.

```sh
gh workflow run gog-staging.yml --repo genixpro/glob2-release --ref master \
  -f tag=v0.11.4.0 -f mode=build -f platforms=all \
  -f reuse_released_payload=true
```

The candidates retain the original source SHA, workflow SHA, released asset
name/digest and a hashed `RELEASE-INVENTORY.json`. Verification compares the
preserved Windows files and Mac app entries against that inventory, including
symlinks and executable modes. Additional depot manifests and source offers must
not overwrite released payload files. Windows and Linux archives are extracted
and verified again before runtime checks.

Windows preserves the portable release's original payload bytes. Linux reuses
the released `usr/bin/glob2`, assets and private libraries, resolving its existing
dependency closure on Ubuntu 22.04. The launcher selects depot libraries without
patching or relinking the executable. Existing private-library bytes cannot be
replaced with host versions. Added system runtime files retain their actual
Ubuntu package/version/source-package records, SHA-256 and copyright files;
referenced common license files are copied too. Missing runtime attribution or
unresolved libraries fail staging. These records describe provenance, not a
blanket rights determination: candidates with added host libraries carry
`runtime_licence_review_required=true` for release review.

The Linux smoke uses a fresh, digest-pinned Ubuntu 22.04 container with explicitly
installed base desktop graphics/audio libraries, not compilers or the game's
codec/TLS development packages. It requires every non-system linked library to
resolve inside the depot and runs the short fixture offline. This is a bounded
packaging check; it does not replace interactive rendering/audio or broader
supported-distribution qualification. The baseline follows GOG's
[Linux dependency guidance](https://docs.gog.com/linux-guidelines/).

Each Mac job mounts its corresponding signed DMG read-only and copies the app
without changing its contents, merging architectures or signing it again.
`SOURCE.txt`, inventory and depot metadata remain **outside** `Glob2.app`.
Native arm64 and Intel jobs verify signatures, stapled notarization and Gatekeeper,
then check a metadata-preserving ZIP roundtrip and run the short native fixture.
Outputs are labelled `gog-released-macos-arm64` and
`gog-released-macos-x86_64`; neither is a universal app.

GOG's [app-bundle depot guidance](https://docs.gog.com/bc-macos-depot/) describes
an intact app depot, while the documented
[architecture selector](https://docs.gog.com/bc-architecture/) is Windows-specific.
These documents do not establish automatic routing between our two Mac CPU
candidates. Confirm the actual product's supported delivery layout with GOG's
publisher tools before uploading either; do not assume Intel-only support or
invent an ARM product/depot selector.

Released-payload reuse rejects `mode=stage` in preflight, and its build jobs have
no GOG credentials or upload dependency. This remains build-only even after
product access is configured. Upload support needs a separately reviewed
architecture/delivery mapping, runtime/license review and actual publisher access.
Keep candidate creation, GOG Staging delivery, QA submission and public release
as separate states.

## Upload to Staging

After GOG grants access, commit the nonsecret product and base product IDs and
the approved Linux Pipeline Builder SHA-256 to `gog/release-config.json` through
review. Set these **secrets** in `gog-staging`:

| Name | Type | Purpose |
| --- | --- | --- |
| `GOG_BUILDER_URL` | Secret | HTTPS URL from which the runner can retrieve that exact executable |
| `GOG_USERNAME`, `GOG_PASSWORD` | Secret | Dedicated upload account credentials |
| `GOG_STAGING_PASSWORD` | Secret | Password for the private Staging branch |

Pipeline Builder is supplied through the Developer Portal; do not redistribute
its binary in this public repository. Arrange a short-lived authenticated URL
for the exact approved binary and pin its hash. If GOG authentication or tool
retrieval cannot run unattended, retain the automated `build` mode and upload
the validated depots manually with Build Creator until GOG provides a supported
CI method.

Run the workflow again in `stage` mode. It rebuilds the depots, validates them
again, then the protected job downloads and verifies Pipeline Builder, renders
the project files and invokes its `--offline` mode before uploading each
platform to the private `Staging` branch. The GOG build name includes the game
version, workflow run ID and attempt number so a retry is distinguishable from
an earlier partial upload. Record the three build IDs shown in the Developer
Portal. The upload job cannot publish to `Master`.

Install and test those exact Staging builds. Submit the first release candidate
to GOG QA in advance of launch. A maintainer promotes the tested build IDs in
the Developer Portal, creates a separate GOG changelog entry, and confirms the
offline installers after publication. Make the exact Corresponding Source
archive available from the store listing as a GOG Extra, and keep its public
commit and archive URL accessible for as long as binaries are offered. For
later stable updates, repeat this
process; leave previous Master builds published so players can roll back.
GOG documents [Staging and Master](https://docs.gog.com/build-branches/),
[build delivery](https://docs.gog.com/build-delivery/),
[changelogs](https://docs.gog.com/updates/) and
[offline installers](https://docs.gog.com/offline-installers/).

[Release index](README.md) · [Documentation index](../README.md).

For a build-only retry, select `platforms` as `windows`, `linux`, `macos`, or
`windows-linux` to retain successful depots from earlier trials. The default is
`all`. Staging requires `all` and the complete platform validation set.
