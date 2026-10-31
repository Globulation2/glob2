# Windows Store releases

Build and qualify Windows Store packages using the restricted release mirror.

## Windows Store release

`.github/workflows/windows-store-release.yml` is a manual `workflow_dispatch` on
`windows-2025`. Its sole job is gated to the owner of `genixpro/glob2-release`
on `master`:
the public `Globulation2/glob2` repository stores the workflow and build code for
review, but dispatching it there cannot build, sign, upload or publish a release.
The owner syncs the public changes into the public release mirror and starts each
release there manually. The mirror's `master` tracks public `master` exactly; see
[the release mirror](releasing.md#the-release-mirror). The workflow builds the existing MinGW x64
client, stages its runtime DLLs, game assets and GPL license, creates
`MicrosoftGame.config`, shell logos and a 1920×1080 splash image, then
uses the Microsoft GDK to produce an MSIXVC package. With `upload: false`, it
creates an installable test-signed package and retains the package and validator
report as a publicly accessible GitHub Actions artifact. With `upload: true`, it
creates a submission-encrypted package and sends it with its encryption key blob
directly to an existing Partner Center branch; neither is retained as a public
artifact. Keep credentials out of workflow logs and artifacts.
The workflow stages the reviewed Microsoft Submission Validator ZIP after verifying
its pinned SHA-256, then passes its directory through `/validationpath`. It does
not rely on `/updatesubval` selecting the expected DLL. Failures are critical with
`/validationcritical`, and the XML engine version must match the reviewed pin. The workflow also checks the actual XML
summary and detailed failure counts before allowing an upload. Failed test-package
reports remain retained for inspection; a successful MakePkg process alone is not
validation evidence. See [Microsoft’s MakePkg options](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/deployment/makepkg).
The workflow does not submit a listing for certification or publish it to retail.

For an existing public Windows release, select the optional `payload_tag` to
repackage its portable ZIP instead of compiling mirror `master`. Supported tags
are explicitly pinned in `.github/scripts/windows_store_portable.py` to a source
commit, asset name and SHA-256; adding another requires reviewing those values.
The adapter verifies the entire archive, preserves every payload file byte,
renames text `.LIB` license notices with a `.txt` suffix, and adds Store identity,
source pointer and shell images derived from the payload’s existing icon. No
current-master game assets or binaries enter that route. The package version is
still an independent, increasing four-part Store version. Start with
`upload: false`, inspect the validator and install-test package, then qualify the
submission-encrypted build before certification. A blank `payload_tag` retains
the fresh mirror-source build route.

Follow the shared [release mirror policy](releasing.md#the-release-mirror) for
repository hardening and credential storage.

The maintained Store identity is recorded below. Confirm it against the
configured release environment and the [Globulation 2 PC game product](https://partner.microsoft.com/en-US/dashboard/products/9PH4FCRMX19F/setup)
in Partner Center before dispatching a release. Its package identity and
initial package branch are:

| Partner Center field | Value |
| --- | --- |
| Store ID | `9PH4FCRMX19F` |
| Package Identity Name | `BradleyArsenault.Globulation2` |
| Package Identity Publisher | `CN=EBC9B192-6200-443B-BFEB-9B00B0B78F67` |
| Publisher display name | `Bradley Arsenault` |
| Package branch | `Main` |

Confirm the product's current certification state in Partner Center. On the
**release mirror only**, create a GitHub Actions environment named `windows-store`,
restrict deployment branches to `master`, and set these environment variables
from Partner Center:

| Variable | Value |
| --- | --- |
| `STORE_ID` | 12-character Store ID (also the Package Uploader Big ID) |
| `STORE_IDENTITY_NAME` | Package Identity Name |
| `STORE_PUBLISHER` | Package Identity Publisher, including `CN=` |
| `STORE_PUBLISHER_DISPLAY_NAME` | Publisher display name |
| `STORE_BRANCH` | Existing Partner Center branch for package upload |

For upload, associate a Microsoft Entra tenant with this Partner Center developer
account. The first release tenant may be temporary. Register an Entra application
in that tenant, grant it **Publishing: Read/Write** for this product,
and add its tenant ID, application ID and client secret as environment secrets named
`STORE_TENANT_ID`, `STORE_CLIENT_ID`, and `STORE_CLIENT_SECRET`.
The `STORE_BRANCH` variable is required for upload. Never add these secrets,
the environment or a privileged trigger to `Globulation2/glob2`.
The mirror's `windows-store` environment allows deployments only from `master`
and requires approval from `genixpro` before the job can access its variables
and secrets. Allow the dispatching owner to approve because that account is
the sole reviewer.

After setting or rotating credentials, run **Actions → Check Windows Store
credentials → Run workflow** on the mirror's `master` branch. Supply the
Application (client) ID and Directory (tenant) ID from the Entra app overview;
the secret's ID is not the application ID. This manual check uses the same
restricted environment, rejects accidental whitespace, compares the stored IDs,
and requests a token for PackageUploader's API resource. It logs neither secrets
nor tokens and does not upload or publish packages. A passing check verifies
authentication; the product's publishing permissions are checked during upload.

When a permanent organization tenant is ready, associate it with the same Partner
Center account, register a new product-scoped publishing application there, and
replace the three `STORE_*` upload secrets in the release mirror environment.
Verify an upload with the new credentials before revoking the temporary
application and removing its Partner Center tenant association. This is a
credential and Partner Center association change, not a tenant transfer. The
Store product identity above stays with the Partner Center product.

After syncing the mirror, run the workflow from its **Actions → Windows Store
release → Run workflow** page on `master`, with a
four-part package version greater than previous uploads, with the fourth part
(revision) set to `0` as required by GDK PC packaging; for example, use
`1.0.1.0` after `1.0.0.0`. Leave `upload` false for
the first run. Download the artifact and install the MSIXVC on a clean Windows PC
with Gaming Services; verify launch, sound, saves, settings, uninstall and a
subsequent version update. Windows saves and preferences default to SDL's per-user
preference directory, while bundled data remains read from the installation
directory. After the package passes installation testing, rerun with `upload` true
and advance the Partner Center submission through its listing, age rating and
certification steps. Keep the corresponding GPL source available with each
distributed version.

## Free retail launch in Partner Center

Package upload is only one part of launch. Maintain the following fields on the
product's `Main` branch before submitting for certification:

1. **Pricing and availability:** select the zero-price tier, confirm the intended
   markets and public discoverability, and review the release schedule. Saving a
   draft does not publish the product. Review any automatic release setting before
   submitting; use a manual hold if certification should finish before launch.
2. **Properties:** Strategy is the primary genre; Simulation is an additional
   genre. Declare PC single-player and online multiplayer. Use
   `https://glob2.online/privacy/` for privacy, `https://glob2.online/` for the
   website, and the current support contact published in that privacy policy.
   Review the customer-visible contact address and phone: Partner Center can use
   the developer account's contact details when product fields are blank.
   Verify hardware requirements against the packaged release; do not infer them
   from an older build or claim untested Xbox or accessibility features.
3. **Store listings:** maintain an English (United States) listing using the copy
   below. Upload genuine desktop screenshots from the release candidate. At least
   one PNG is required; Partner Center recommends four, at 1366 × 768 or larger.
   The [approved storefront gallery](https://github.com/Globulation2/glob2-marketing/tree/a968359dc95d51e998f63ba4d984d33b04560b39/assets/storefront-approved-v1)
   supplies PNG exports of the River battle, developed Four Squares strategic
   view and Lava Shield terrain at 2560 × 1440. Keep their full HUDs and inspect
   them against the Windows release candidate before submission. The colony
   image remains 1280 × 720; do not upscale it to imply a new capture. Keep
   illustrated key art separate from gameplay screenshots. Do not reuse mobile
   frames or the historical 0.9.5 screenshot as evidence of the current Windows
   release. Package logos are the default; optional Store poster and box artwork
   can use the approved illustration after checking the destination crop.
4. **Age ratings:** reuse an applicable existing IARC certificate if available,
   otherwise complete the questionnaire accurately. Disclose cartoon/fantasy
   combat and native online communication. Review the included content and whether
   the Windows client offers digital purchases or links to checkout; a free base
   game alone does not settle those answers. Do not equate a developer audience
   policy with an IARC rating.
5. **Additional Testing Information:** explain offline single-player, campaigns,
   editor, save/load, guest online play and LAN testing. Guest online play does not
   require an email/password; do not supply administrator or publishing credentials
   to reviewers. Verify these instructions against the candidate build.
6. **Review and submit:** check the exact package/version, listing, rating, pricing,
   markets and release controls together. Obtain the owner's launch decision,
   submit for certification, resolve feedback, and publish using the selected
   release controls. The workflow does not perform these steps.

Gaming metadata (supported languages and verified accessibility features) is
optional. Keep claims aligned with the actual shipped client. Keep the matching
GPL source and license accessible for every distributed version.

### English listing copy

**Short description**

Build a thriving colony in contested territory. Balance food, workers and expansion, shape a sprawling base, and compete with rival colonies for land and resources.

**Description**

Build a thriving colony in contested territory.

Globulation 2 is a real-time strategy game where food, workers and space shape your expansion. Grow a sprawling colony, balance its competing needs, and contend with rivals for land and resources. Your creatures act autonomously, letting you guide the colony as a whole.

Grow an economy that can sustain your ambitions.
Food, construction and training compete for your workforce. Decide where workers are needed and balance new ambitions with the needs of the colony you already have.

Make the map part of your strategy.
Different maps call for different strategies. Adapt your expansion to the terrain, choose what to defend, and find the routes and resources your colony needs.

Defend what you've built.
Your economy supports both growth and defense. Balance the needs of your colony while rivals compete for land and resources, and prepare for conflicts that can decide the match.

Globulation 2 is free and open source.

Single-player games can be played offline without an account. Online multiplayer
requires an internet connection.

**Product feature:** Balance food, workers and expansion while guiding autonomous creatures.

**Developed by:** Globulation 2 contributors.

**Search term:** real time strategy.

The repository packages the Store build as original MSIXVC. A future MSIXVC2 migration needs its own Partner Center package branch.
See Microsoft's [PC packaging guide](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/overviews/packaging-getting-started-for-pc),
[MakePkg reference](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/deployment/makepkg),
and [Package Uploader setup](https://github.com/microsoft/PackageUploader).

[Release index](README.md) · [Documentation index](../README.md).
