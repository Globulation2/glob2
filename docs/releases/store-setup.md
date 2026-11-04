# Desktop store setup

Configure store credentials and mirror-only publication workflows.

## Store presentation

Use the [approved colony/economy copy and media](https://github.com/Globulation2/glob2-marketing/blob/b759591/planning/approved-storefront-rollout.md)
when maintaining Epic, Steam, GOG, itch.io or other project-controlled listings.
The [desktop export pack](https://github.com/Globulation2/glob2-marketing/tree/a968359dc95d51e998f63ba4d984d33b04560b39/assets/storefront-approved-v1)
records gameplay order, key-art/logo sources and image provenance. Adapt to each
store's fields and crop preview, keeping illustrated key art separate from
actual gameplay. Use device-specific release-candidate captures for mobile
screenshot slots. Listing updates and binary release/availability are separate
operations; preserve each platform's existing release controls.

## Canonical listing translations

The [marketing translation repository](https://github.com/Globulation2/glob2-marketing/tree/main/translations)
owns the approved English master, terminology, 32 translated catalogs, source
fingerprints and independent AI review records. Change that source first when
editing listing language; translations need renewed review when their English
source or translated text changes. AI review is not native-speaker validation.

In a checkout of the marketing repository, validate and generate merge inputs:

```sh
python3 translations/tools/storefront.py check --require-review
python3 translations/tools/storefront.py export --out /tmp/glob2-storefront-export
```

Merge the generated Fastlane locale text into `fastlane/metadata/android/`,
preserving existing changelogs and release metadata. This tree feeds F-Droid;
its neutral locale directories are deliberate. The separate `google-play/`
export uses Play locale identifiers and still requires console verification.
Do not upload the F-Droid directory tree as ready Play metadata. Merge AppStream summary,
description paragraphs and screenshot captions into the corresponding existing
nodes in `content/data/org.globulation2.Globulation2.metainfo.xml`; the exported fragment
is not a replacement component and its `source_role` hints are not AppStream
attributes. Preserve release history, approved screenshot URLs and factual
edition-specific qualifications. The desktop fragment supplies translated
`Comment` values for `content/data/glob2.desktop`; preserve launcher settings and do not
infer a new `GenericName` translation from a different marketing field.

Console fields and locale availability require separate verification. Source
metadata changes do not publish store listings. Reuse language-neutral approved
artwork, preserve native gameplay UI and the product wordmark, and translate
only actual marketing overlays from approved source images. Snap localization
is unverified; keep its maintained approved English fields until native support
is established. Never concatenate multiple languages into a single listing.

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
`epic-windows-release.yml` there with the tag. Leave `upload=false` (the default)
for packaging and runtime smoke verification only; set `upload=true` to request
the credential-bearing Dev upload after those checks pass. Its preflight checks the public
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

The native CLI accepts the documented Epic Launcher `-KEY=VALUE` fields and
`-EpicPortal` switch without treating them as game commands. Their values are
discarded instead of copied into the parsed game request; this does not add EOS
authentication or change the game's account flow. Unsupported arguments still
fail validation. Test Launcher-style arguments as well as the headless smoke
command when qualifying the candidate.

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
`snap-release.yml` workflow defaults to building and testing an unpublished
candidate on manual dispatch (`publish=false`), without requiring store credentials.
Enable `publish` and select `candidate`, `beta`, `edge`, or `stable` to upload
the newly built package to that channel. Calls from `publish-desktop.yml` retain
their stable-publication behavior. Security notices about Ubuntu stage packages
require a fresh build, even when the game source tag has not changed. Retain the
new package and verify its bundled package versions against the notice, then
install and play it before stable promotion. Each channel pointing at the old
revision needs updating or closing; publishing stable alone leaves other
channels on their previous revision.

Configure the
`PUBLIC_RELEASE_GH_TOKEN` secret in the `github-release` environment
with permission to create releases in `Globulation2/glob2`. The
`github-release.yml` workflow stages final packages and `package-inventory.json`
in a draft GitHub release. Its inventory normalizes Snap/RPM filenames and
includes all eleven playable packages plus the tagged source archive.
`promote-downloads.yml` independently downloads and validates these bytes before
publication; see the [qualified manifest procedure](downloads.md#qualified-downloads-manifest).

The Flatpak recipe in `platforms/linux/flatpak/org.globulation2.Globulation2.yml.in` pins every
third-party source and is rendered with the selected commit for our package
builds. Before preparing a Flathub submission, read its current
[requirements](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy).
Flathub prohibits AI-generated or AI-assisted manifests and AI agents opening
submission PRs or generating their commit messages, descriptions or review
replies. It also requires disclosure of known AI-generated material in the
application, identifying the affected parts and approximate extent. Our
AI-assisted recipe can be used for our own Flatpak builds; it is not an eligible
Flathub submission manifest. A human maintainer must author the submission
manifest and interactions under Flathub's policy.

Once the selected stable candidate passes build, lint and installed-game checks,
the human submitter forks `flathub/flathub` with its `new-pr` branch, adds their
manifest at the fork's top level, and opens a first-listing PR targeting `new-pr`
under `org.globulation2.Globulation2`. Acceptance creates the public listing.
Flathub publishes from its
own manifest repository after its review; a GitHub release cannot directly
publish to Flathub. Once the Flathub app repository exists, create a
writable fork and set `FLATHUB_FORK` (an `owner/repository` GitHub Actions
variable) and `FLATHUB_GH_TOKEN` (a secret with permission to push that fork and
open a PR). `flathub-update.yml` then opens a Flathub update PR. A Flathub
maintainer must review and merge it; the workflow does not bypass that review.
Keep the upstream AppStream metadata in `content/data/` updated with each release's
notes and screenshots.

[Release index](README.md) · [Documentation index](../README.md).
