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
uses a pinned Microsoft GDK to produce an MSIXVC package. The payload has no
embedded GDK version, so the validator judges the packaging tools' GDK; raise the
GDK pin together with the validator pin once a validation passes on the new pair.
With `upload: false`, it
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

```text
A free real-time strategy game with no ads and no in-app purchases. Grow a colony and fight rivals for land. You set the priorities; your colony does the work. Optional: play alongside your own AI agent through the built-in MCP server.
```

**Description**

```text
Real-time strategy where you command the colony, not every unit.
Free. No ads. No in-app purchases. No loot boxes. No premium currency.

Globulation 2 is a real-time strategy game about a colony of globs: small, busy creatures that work, eat, train and fight. You grow the colony, keep it fed and fight rival colonies that want the same land.

You decide. Your globs do the work.
Choose what to build and how many globs should work on it. They find their own way, gather the materials and build it, and they eat when they are hungry. To explore new land or attack a rival, place a flag and set how many globs should go. There are no units to select one by one: you plan for the whole colony.

Keep the colony fed.
Inns feed your globs. Swarms produce more of them. Schools, racetracks, swimming pools and barracks train them, and hospitals heal them. All of it competes for the same workers, so every new project takes workers from another: a bigger army is no use if nobody is left to harvest the wheat.

Plan where to expand.
Wheat, wood and stone are rarely where you want them, and the routes between them matter as much as the resources. Decide where to expand, what to protect with walls and what is worth fighting for. Play 23 handmade maps, or generate a new map from any of 67 random landscapes.

Fight for the land.
Rival colonies are growing too, and they want the same land. Train warriors, build defense towers and stone walls, and place war flags where your army should attack. An attack lasts only as long as the food and hospitals that support it, so a war is won at the inns as much as at the front.

What's in the game.
• 23 handmade maps, plus new maps generated from 67 random landscapes
• 8 computer opponents, from easy to hard
• Up to 16 colonies in one match
• A 10-lesson tutorial
• Built-in map editor
• Save and watch replays
• Offline single-player, with no account needed
• Write your own computer opponent in JavaScript
• Available in 45 languages

Play together.
Play over your local network with no account and no internet connection.

Play alongside your own AI agent.
Globulation 2 has a built-in MCP server. Turn it on in Settings, and an AI agent on your computer can see the game and play the same colony with you. Your mouse and keyboard keep working the whole time, so you can divide the work as you like: let the agent manage the food supply while you plan the attack, or just watch what it does. This works in local games against computer opponents. It is optional: the server is off unless you turn it on, and it runs only on your own computer. You bring your own AI agent; the game does not include one and is complete without one.

Open source, built by volunteers.
Globulation 2 started in 2001 and was released to the public in 2004. Volunteers have built it over two decades. It is free and open source under the GPL, version 3: the code is public, and anyone can read it, change it and help improve the game.
```

This copy leaves out online play until the Store package is current with the
online platform.

**Product features**

1. Command the colony, not every unit: you choose what to build and where to attack, and your workers and warriors do the work on their own
2. Free. No ads. No in-app purchases. No loot boxes. No premium currency.
3. Optional: play the same colony alongside your own AI agent through the built-in MCP server
4. 23 handmade maps, plus new maps generated from 67 random landscapes
5. 8 computer opponents, from easy to hard
6. Up to 16 colonies in one match
7. Offline single-player, with no account needed
8. Built-in map editor

**Search terms:** real time strategy, RTS, base building, colony, offline strategy, open source, MCP AI agent game.

**Screenshot captions**, in the desktop gallery order:

1. Command a whole colony: workers, buildings and warriors in the middle of a match.
2. Choose a building and how many workers it gets; they gather the materials and build it.
3. Workers carry wheat to the inns and swarms that feed and grow the colony.
4. Warriors from rival colonies fight for the same land.
5. A wider view of the map shows where rival colonies meet.
6. One of the game's maps, each with its own terrain, resources and routes.
7. Several colonies share one map and compete for the same land.
8. A local game shared with the player's own AI agent, connected through the built-in MCP server.

**Trailer title:** Globulation 2: gameplay trailer

**Developed by:** Globulation 2 contributors.

**Search term:** real time strategy.

The repository packages the Store build as original MSIXVC. A future MSIXVC2 migration needs its own Partner Center package branch.
See Microsoft's [PC packaging guide](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/overviews/packaging-getting-started-for-pc),
[MakePkg reference](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/deployment/makepkg),
and [Package Uploader setup](https://github.com/microsoft/PackageUploader).

[Release index](README.md) · [Documentation index](../README.md).
