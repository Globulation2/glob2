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
The workflow does not submit a listing for certification or publish it to retail.

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

The repository packages the Store build as original MSIXVC. A future MSIXVC2 migration needs its own Partner Center package branch.
See Microsoft's [PC packaging guide](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/overviews/packaging-getting-started-for-pc),
[MakePkg reference](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/deployment/makepkg),
and [Package Uploader setup](https://github.com/microsoft/PackageUploader).

[Release index](README.md) · [Documentation index](../README.md).
