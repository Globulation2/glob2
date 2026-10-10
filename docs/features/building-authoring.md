# Building authoring

Companion to [building catalogs](building-catalogs.md).

## Private authoring workspace

To create and share a family:

1. Sign in to the website with a registered account, open **Buildings**, then
   **Create a building family**. Give the draft a descriptive name.
2. Edit the initial variant's properties and capabilities. Keep its key stable;
   display names can change without rewriting references. Add stages for explicit
   construction and upgrade transitions, and link them using `next` and `previous`.
3. Reuse an installed stock sprite or add custom sprite frames in the artwork
   controls. Team-color layers are optional and must match the base image's size.
4. Save the draft. Fix validation errors before publication. JSON editing is useful
   for advanced capabilities; the engine validates the saved package as a whole.
5. Choose a description and visibility, then publish the saved revision. Wait for
   native validation; rejected releases explain the error. Further draft changes
   need another publication to appear in the library.
6. In the game, open **Building families** on a local new-game or editor new-map
   screen, browse public families or paste the family's page link into **Family
   link or ID** and choose **Open family**. Private families need sign-in to the
   same instance. Install the compatible release and enable it. Selection applies to new
   maps in that profile. To play it online, share the resulting map and select it
   in the room. Downloading a ZIP on the website exports the authored package.

Common authoring controls include:

| Control                         | Meaning                                                                                                                                                                             |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `hpInit`, `hpMax`               | Health on creation and maximum health; initial health cannot exceed the maximum                                                                                                     |
| `isBuildingSite`                | Construction/repair stage; its `next` must name a completed variant                                                                                                                 |
| `level`                         | Display tier, 0–3; it does not infer an upgrade link                                                                                                                                |
| `placeable`, `instantPlacement` | Whether players can place the variant; a completed placeable variant needs instant placement, otherwise use a construction site                                                     |
| `repairable`, `previous`        | Repair capability and explicit site reference; repairable variants need a positive maximum health and a previous construction site                                                  |
| `constructionCost`              | Costs attached to a construction site; completed variants cannot carry them. AI-authored sites with material costs must have a positive `assignmentLimit` so workers can build them |
| `requiredExperiment`            | Package-local experiment gate; an unavailable gate keeps the variant out of the new game's choices                                                                                  |

The service and presentation sections earlier in this guide describe the remaining
capabilities. Definitions compose existing primitives; publication does not add
new executable game behavior.

The web app's `/building-studio` page creates private building-family drafts for
registered accounts. It provides scalar property controls, nested JSON controls,
complete manifest editing, upgrade-stage links, experiment definitions and sprite
frame previews. Authors can upload a base frame or an optional team-color layer,
import a package, and export the last saved package. Exporting preserves the
family namespace; importing a package replaces the selected draft's content.

`/api/v1/building-drafts` lists and creates drafts. The draft resource supports
reading, saving and deleting; `/archive` imports and exports ZIP packages;
`/frame` replaces or appends a frame; and `/assets/<hash>` serves an owned draft's
normalized artwork. Saves and uploads require the current revision UUID and
reject stale updates. Changing a manifest cannot invent missing assets. Uploads
retain only normalized, still, lossless WebP bytes, and team-color layers must
match their base frame dimensions. Account exports include the saved ZIP bytes;
account deletion removes private drafts.

The workspace limits each account to 100 drafts, 64 MiB of saved packages, and
60 saves or uploads per hour.
Device recovery copies supplement account drafts; authors explicitly restore a
copy instead of silently replacing a newer account revision. These are authoring
interfaces. Publishing a saved revision submits an immutable release for native
validation; later edits do not rewrite that release.

## AI Building Studio

`/ai-building-studio` is a separate conversational workspace for creating and
revising building families. Start a project or choose **Edit with AI** from an
owned manual draft. Fork a library release into a draft to revise it with AI.
Questions and brainstorming are free. A clear creation or edit request starts
one generation automatically; each validated delivery costs one building credit,
including property-only revisions. Confirmed failures return the reservation;
unknown provider outcomes retain it for operator reconciliation without automatic
redispatch. The cost is displayed beside the conversation composer.

The studio composes existing engine capabilities and follows the author's desired
properties without judging balance or power. Unsupported mechanics require a
conversation about alternatives. New buildings default to one completed stage
and a construction variant; upgrade stages are added when requested. Default art
matches the stock sprites' elevated, approximately 45-degree downward camera,
with visible roof surfaces and foreshortened walls; all stages share that camera.
Stock camera references remain present alongside player uploads. Completed
artwork is generated first and supplied as an identity reference for its
construction stage, keeping each family visually coherent.
Appearance edits preserve gameplay fields, and property-only edits reuse existing
artwork.
Ordinary generated sprites are still images; connected-segment artwork and
animation need manual authoring in this release. Generated artwork supports
footprints up to 12 tiles per side; existing larger buildings can still receive
property-only edits.

Projects are limited to 100 per account, with at most 100 saved AI revisions
and 64 MiB of candidate archives across projects. Export and delete project
history to reclaim this allowance; its ordinary manual draft remains available.
Reference uploads have a separate 64 MiB account allowance and a limit of 64
images per project; up to four may be selected for one request.

The preview shows packaged frames at game scale or enlarged scale over grass,
sand or water swatches, with readable properties and revision comparisons.
These are authoring previews, not simulated behavior trials. Native composition
validates the exact self-contained archive before delivery. A candidate applies
only if its starting draft revision UUID still matches; otherwise it remains
available for review and explicit restoration. Restoring an earlier revision is
free and also checks the current draft UUID. Export any candidate as a ZIP, or
open the current draft in Building Studio to edit and publish through the
existing library workflow. AI generation never publishes automatically.

## Online library and installed families

`/buildings` browses the public library; each family page lists pinned releases,
validation state, simulation version, downloads, likes, favourites, reports and
forking into a private Studio draft. Publication defaults to unlisted. Only an
active registered account can publish, and the namespace remains owned by its
first publisher. Public, unlisted and private visibility follow the same access
rules as the map library. Hidden families cannot be downloaded or installed.
Moderators can hide/restore families and resolve their reports. Account export
includes drafts, published families, release metadata and the account's social
activity; account deletion removes that authored library content.

Owners can edit a family's published name, description and visibility without
revalidating its releases, or withdraw the family after confirmation. Withdrawal
removes its library releases and social activity; it leaves the Studio draft and
maps, saves and replays that already embed the family's data intact. These
metadata changes do not change catalog identity. Moderation and owner changes
are recorded in the administration audit log.

Moderators retrieve the unresolved report queue with
`GET /api/v1/building-reports`, hide or restore a family with
`PUT /api/v1/buildings/:id/moderation` (`hidden` and `reason`; hiding requires a
nonempty reason), and mark a report resolved with
`PUT /api/v1/building-reports/:id` and `{"resolved": true}`. Hiding a family does
not itself resolve its reports. The website exposes hide/restore on a family
page; the report queue and resolution workflow currently use these API endpoints.

`POST /api/v1/building-drafts/:id/publish` freezes the exact saved archive and
submits `validate-buildings` to a fresh engine agent that advertises
`compose_buildings`. Suite 1 checks package closure, deterministic composition,
stock identity, sprite references and bounded full artwork decoding. A validated
release is bound to its archive hash, stock hash, simulation version and suite.
Unavailable engines leave publication unavailable; pending/rejected releases
cannot be downloaded. Families retain at most 50 releases and publishing/forking
is limited to 20 requests per account per hour. Reports are limited to 10/hour.

The native new-game and new-map screens open **Building families**. The picker
installs a chosen compatible release, verifies its package and artwork hashes
and resolved catalog, and lets players explicitly enable or remove families.
Unlisted families open by their page link or ID; links must belong to the
selected instance. Private family lookup uses that instance's signed-in account.
Installed releases stay pinned until another release is chosen. The cache is
limited to 100 families and 64 MiB; combined manifests and decoded pixels retain
the composition bounds above. Selection is applied before new-map generation.
Loaded maps and ongoing/saved games retain their own definitions and artwork.
Share a newly created map through the existing map library to use those families
in an online room; every participant and verifier receives the map's embedded
catalog and frames. No family cache or original library availability is needed
to load a shared map, save or replay.

Existing map-sharing limits still apply: uploads default to 16 MiB of transferred
bytes, and the native map cache and engine agent accept at most 64 MiB of raw map
data. A valid local package or combined artwork bundle can exceed those limits;
use smaller frames or fewer families when preparing an online map. The bundle's
wire format is limited to 72 MiB, and each mount is limited to 64 MiB of encoded
frame bytes. The 64 MiB decoded-pixel bound applies to each composition. The
Toolkit also retains
previously loaded sprite objects for the session, so repeated loading of distinct
custom artwork can accumulate decoded residency beyond the current bundle's
bound; replacing the mounted bundle does not free those retained sprites.

The native installation API uses a validated release's `/runtime` descriptor
and `/artwork` bundle, while `/archive` exports its portable ZIP. The runtime
descriptor carries the exact canonical package JSON as a string and its hash,
avoiding numeric-serialization differences between JavaScript and C++. All
release resources recheck visibility and moderation on every request.

For command-line new maps, write the composed catalog snapshot to a JSON file
and pass `--building-catalog` with `--building-artwork` to `map generate`.
The latter accepts the verified `G2BA0001` bundle, which is embedded in the output.
Format 145 and network protocol 62 introduced this artwork header layout.
Current save format and replay acceptance are 150, with network protocol 68;
the save-support floor remains 58.

Related: [features and content](README.md).
