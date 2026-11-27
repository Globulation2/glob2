# Desktop store setup

Configure store credentials and mirror-only publication workflows.

## Store presentation

Use the [storefront-v2 listing copy](https://github.com/Globulation2/glob2-marketing/tree/19d01492a8861debe7c0baefadcd49679c3f85a0/translations)
when maintaining Epic, Steam, GOG, itch.io or other project-controlled listings;
the [storefront-v2 rollout](https://github.com/Globulation2/glob2-marketing/blob/main/planning/storefront-rollout.md)
records which stores have it. Until the storefront-v2 screenshots and key art are
hosted, the [storefront-v1 desktop export pack](https://github.com/Globulation2/glob2-marketing/tree/a968359dc95d51e998f63ba4d984d33b04560b39/assets/storefront-approved-v1)
records the current gameplay order, key-art/logo sources and image provenance. Adapt to each
store's fields and crop preview, keeping illustrated key art separate from
actual gameplay. Use device-specific release-candidate captures for mobile
screenshot slots. Listing updates and binary release/availability are separate
operations; preserve each platform's existing release controls.

## Canonical listing translations

The [marketing translation repository](https://github.com/Globulation2/glob2-marketing/tree/main/translations)
owns the approved English master, terminology, 44 translated catalogs
(45 including English), source fingerprints and independent AI review records.
Change that source first when editing listing language; translations need renewed
review when their English
source or translated text changes. AI review is not native-speaker validation.

In a checkout of the marketing repository, validate and generate merge inputs:

```sh
python3 translations/tools/storefront.py check --require-review
python3 translations/tools/storefront.py export --out /tmp/glob2-storefront-export
```

Merge the generated Fastlane locale text into `fastlane/metadata/android/`,
preserving existing changelogs and release metadata. This tree feeds F-Droid;
its neutral locale directories are deliberate and cover all 45 canonical locales.
Keep F-Droid directory mappings separate from console locale identifiers.
The separate `google-play/` export uses Play locale identifiers and still
requires console verification.
Do not upload the F-Droid directory tree as ready Play metadata. Merge AppStream summary,
description paragraphs and screenshot captions into the corresponding existing
nodes in `content/data/org.globulation2.Globulation2.metainfo.xml`; the exported fragment
is not a replacement component and its `source_role` hints are not AppStream
attributes. Each caption belongs under the screenshot its role describes, in the
order of `asset_text.desktop_gallery_order` in the marketing `platforms.json`;
until the storefront-v2 desktop gallery is hosted, the four storefront-v1 images
carry the `economy`, `battle`, `territory` and `maps` captions. Preserve release
history, approved screenshot URLs and factual edition-specific qualifications. The desktop fragment supplies translated
`Comment` values for `content/data/glob2.desktop`; preserve launcher settings and do not
infer a new `GenericName` translation from a different marketing field.

Console fields and locale availability require separate verification. Source
metadata changes do not publish store listings. Reuse language-neutral approved
artwork, preserve native gameplay UI and the product wordmark, and translate
only actual marketing overlays from approved source images. Snap localization
is unverified; keep its maintained approved English fields until native support
is established. Never concatenate multiple languages into a single listing.

### Apple metadata localization

`platforms/mobile/apple_store_localize.py` maintains only reviewed listing text
for the 38 canonical Apple locales, including English. `prepare` consumes the
strict marketing exporter and, for iOS, the independently reviewed version-specific
release-note manifest. Both the public tooling and marketing checkouts must be
pinned to complete commit hashes; modified or untracked translation sources fail.

The owner-only `apple-store-localize.yml` workflow runs `prepare` without Apple
credentials, then uses the existing protected `ios-testflight` environment for
`inspect` or `apply`. Its reports contain counts and digests, never raw store
snapshots, review contacts or authentication material. The explicit platform,
version and already-attached build must match an unsubmitted editable draft
(`PREPARE_FOR_SUBMISSION` or `DEVELOPER_REJECTED` after withdrawal). The helper
never withdraws or submits a review; waiting/in-review and distribution states
remain blocked.
For the current candidates these are `IOS` / `0.11.601` / `1002501` and
`MAC_OS` / `0.11.6` / `2026111801`.

Run `inspect` first and supply its `baselineSha256` to a separate `apply` run with
the identical source/target inputs. Any intervening listing, media, build or
release-option change invalidates that baseline. Run platforms sequentially:
iOS inspect/apply, then a fresh Mac inspect/apply, because AppInfo is shared and
Apple may automatically create related localization records on other platforms.
If another platform has already submitted the shared AppInfo, the helper may
read that pending record only when all 38 reviewed names/subtitles already
match exactly; it never changes pending shared information or selects live info.
After an interrupted apply, inspect again; already-correct fields are skipped
and existing resources are never deleted or blindly recreated.

Writes are limited to AppInfo name/subtitle and version description, promotional
text and keywords, plus reviewed iOS What's New. Mac What's New is preserved.
New locale URLs inherit the current English privacy/support/marketing values;
populated existing URLs, privacy text and other fields remain exact. Existing
mapped platform locales with a blank required support URL also inherit the
current primary-locale support URL; optional blank URLs stay blank. New screenshot and
preview sets stay empty for primary-language fallback. Complete readback checks
all intended values, existing asset identities/order and unchanged fields, and
accepts only explained empty/primary-inherited automatically created locales.
Inspect representative native/RTL presentation separately. This workflow never
changes builds, review submissions, release type, prices, availability or media,
and does not submit or publish a product.

## Store setup

Publish an approved version before staging or submitting anything that would
replace it. Several stores approve a version and then wait for a manual
publish step: Epic Stage entities reading "Ready for Live", a Partner Center
submission set to publish manually, an App Store version in "Pending Developer
Release", and a Play release held by managed publishing. Pushing new changes
over an approved version restarts review and can leave the product with no
public listing; Epic approved this product twice in late 2026 without either
version reaching Live. Hold an approved version back only on a deliberate
decision, and check submitted reviews at least daily so an approval is
published as soon as it lands.

### Steam

The existing Steam pipeline stages **Windows only**. Finish Steamworks partner
onboarding and create the product before configuring CI; the pipeline does not
create an account, pay the app fee, or complete legal, tax or banking declarations.
Use the assigned product AppID and its Windows content DepotID, never example IDs.
Configure the installation directory and the Windows launch executable
`glob2.exe`, publish the app configuration, and verify the depot is included in
the developer/test package.

Follow Valve's [SteamPipe setup and CI instructions](https://partner.steamgames.com/doc/sdk/uploading).
Use a dedicated build account with access to this app and the **Edit App Metadata**
and **Publish App Changes To Steam** permissions. Authenticate it interactively
with SteamCMD on a trusted machine, completing Steam Guard there, then verify
passwordless login with its saved `config/config.vdf`. Treat that file as a
credential: base64 encoding does not make it public-safe.

In `genixpro/glob2-release`, configure the protected `steam-release` environment:

| Kind | Name | Value |
| --- | --- | --- |
| Variable | `STEAM_APP_ID` | Assigned numeric AppID |
| Variable | `STEAM_WINDOWS_DEPOT_ID` | Assigned numeric Windows DepotID |
| Variable | `STEAM_BUILD_USERNAME` | Dedicated account's Steam login name |
| Secret | `STEAM_CONFIG_VDF_BASE64` | Base64-encoded authenticated `config.vdf` |

Transfer the credential directly from the local file to the environment secret;
never paste it into issues, logs, public variables or this repository. Valve
notes that SteamCMD can update the saved login configuration. The current runner
does not save those updates back to GitHub; refresh the secret from a trusted
authenticated session if login stops working. Do not disable Steam Guard to
work around a login failure.

Dispatch `steam-windows-package.yml` from the release mirror's `master` with an
immutable public release `tag`, such as `vVERSION`. Its preflight resolves that
tag in `Globulation2/glob2`; release compilation, packaging and the Windows runtime
smoke test must all pass. The output `glob2-steam-windows-<source SHA>` includes
the game, assets, DLL closure, `SHA256SUMS.txt` and `source-commit.txt`.

When the account configuration is ready, dispatch `steam-windows-upload.yml`
from that same mirror branch with the selected tag. It rebuilds and tests the
candidate before requesting access to `steam-release`, then checks depot hashes
and source identity before SteamPipe upload. The environment requires an approval
from `genixpro`; retain that gate. This creates an **unpublished build** and does
not set a Steam branch live.

Verify the uploaded BuildID and Windows depot in Steamworks, assign it to a test
branch and install through Steam. Check launch, assets, campaign play, save/load
and multiplayer before promotion. Build upload, test-branch availability, store
review, build review and public release are separate states; complete Steam's
release checklist and verify the public result before recording the game as live.
The current pipeline supplies no macOS or Linux Steam depot and makes no Steam
Deck qualification claim.

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
it to Live in the portal. Before every Push to Stage, open Release Management:
if any Stage entity reads "Ready for Live", push the approved entities to Live
first. An offer whose available date is unknown goes Live as a "Coming soon"
page without a download, so that needs no binary or date.

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

### itch.io desktop downloads

After the stable GitHub release is public, dispatch
`itch-release.yml` from the release mirror's `master` with that tag. The default
`verify` mode checks the current release's package inventory and GitHub asset
digests for the Windows portable ZIP, both macOS DMGs and the Ubuntu-compatible
Linux tarball. It stages the signed, stapled app bundles from the DMGs and the
Linux archive's executable and runtime tree with an itch launch manifest.
The Windows payload adds a **Play (software renderer)** action for machines
without working hardware graphics, alongside the standard Play action.
The release mirror provides the expected signing team via `MACOS_DOWNLOAD_TEAM_ID`.
Verification requires no itch.io credentials.

Keep `BUTLER_API_KEY` in the mirror's protected `itch-release` environment,
restricted to `master`. See [Butler CI authentication](https://itch.io/docs/butler/login.html#running-butler-from-ci-builds-github-actions-gitlab-ci-etc).
Use `create-hidden` for the first push. In the itch dashboard, confirm all
four channels have the expected platform flags and version. Verify install and launch in the itch app for the Windows ZIP, each macOS
architecture and the Linux archive on a supported system. DMGs and Flatpak
remain available on GitHub for manual installations.
Then make the new channels visible and hide the corresponding manual uploads.
The browser launcher follows the production web deployment separately.

For later releases, select `publish` to update the same channels. Each upload
can go live independently. If publication stops partway through, check which
channels received the version, then redispatch `publish` with the same tag while
it is still the latest stable release. During the initial hidden setup, check
which channels already exist. Retry `create-hidden` for each missing channel
using the channel selector. Butler's `--hidden` option only works on new channels. See [Butler channel behavior](https://itch.io/docs/butler/pushing.html).

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
