# Desktop store setup

Configure store credentials and mirror-only publication workflows.

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
publication; see the [qualified manifest procedure](downloads.md#qualified-downloads-manifest).

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

[Release index](README.md) · [Documentation index](../README.md).
