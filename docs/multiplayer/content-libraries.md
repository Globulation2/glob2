# Terrain, resources and generator libraries

Publishing custom terrain/resource sets and shared JavaScript generators.

## Terrain and resource set library

`sets.ts` in the protocol package defines the bounded set package and draft,
publication and validation contracts. `asset_sets` supplies owner-controlled metadata
and visibility; `set_drafts` stores revisions and the job/hash that owns current
validation; `set_versions` stores immutable packages and engine previews. Likes,
daily download counts and moderator reports use separate tables. The REST surface
is `/api/v1/sets` and `/api/v1/set-drafts`, with report handling under
`/api/v1/admin/sets`. Catalog pages use cursors tied to their sort order. Set listings omit release notes
and contributor details, which are returned by the individual set endpoint. Draft
listings return titles and identifiers with stable `updatedAt`/identifier cursor
pagination (`limit` defaults to 100); individual draft reads include the package.

The web workspace uploads PNGs into the package, maps frames to custom entries,
and saves properties and credits. `/play/set-preview.html` runs an ephemeral browser
engine against the current draft for an interactive preview request. It uses a
temporary profile, validates the message origin/source/run/revision, and never
mounts the player's persistent saves. This preview does not authorize publication.

All jobs from asset-capable engines (file format 144 onward) run through a shared
isolated launcher, including map validation, previews and match verification:
uploaded maps can contain the same bundled PNGs as standalone sets. A failed
launcher probe prevents the agent from starting. Server publication checks are
`validate-set` jobs, advertised only when additionally enabled. Set `ENGINE_SET_VALIDATION=1` after validating the deployment.
The startup probe requires Linux bubblewrap, a dedicated scratch tmpfs of at most
4 GiB and the engine inside a namespace without networking. Installed data and
engine/runtime libraries are read-only; the job alone gets writable scratch. The
probe starts the engine and renders a small set before advertising the capability.
`ENGINE_SET_LIBRARY_PATH` can supply additional runtime library directories. Standalone
set checks are bounded to 120 seconds, 2 GiB address space and 64 MiB per output file.
Without a successful probe the asset-capable agent cannot serve engine jobs;
drafts can still be saved. Standalone set publication checks are disabled by
default in engine-agent deployments.

Results apply only while the draft still owns the exact job and hash. The stale-job
sweep requires a fresh agent advertising the job kind as well as its simulation
version; losing the set capability cannot leave an old check pending indefinitely.
Publication locks the parent and draft and requires passing checks on the requested revision.
A set may retain 50 releases and 100 drafts; immutable release labels and hashes
cannot be reused. Blob collection retains draft inputs, checked previews, queued
inputs and published packages. Account exports include owned sets, drafts, versions,
likes, reports and download rows; account deletion removes those owned rows.

Map validation also extracts bundled set attribution for shared-map version pages.
Moderating or withdrawing a source set never invalidates a self-contained map.


## JavaScript generator library

`/api/v1/generators` provides catalogue, release, file, preview, social and reporting
routes; owner-only `/api/v1/generator-uploads` stages packages and exposes validation
results before atomic publication. Public/unlisted/private access and moderation
follow maps. `generator_ids` permanently reserves a manifest ID at first successful
publication, including after deletion or account deletion. Releases are immutable
and require increasing manifest revisions. Canonical package bytes come from the
engine parser rather than a second TypeScript canonicalizer.

`validate-generator` and `generate-script-map` use the common Linux namespace
launcher for package inspection, generation and saved-map reload. The worker probes
the real isolated engine before advertising these capabilities. Validation evidence
binds source and canonical hashes, example settings, simulation version and suite;
the report includes API/toolkit versions, sampled settings, fingerprints and refusals.
Maintenance revalidates retained releases for newly served engine versions and
collects abandoned staging uploads without deleting historical release evidence.
See [JavaScript generators](../map-generators/javascript.md#publish-and-discover-online)
for the validation matrix, budgets and author workflow.

Room selection and match setup use a separate `scripted` map source with an exact
library/release identity, package and file hashes, namespaced ID/revision, request
and resulting map hash/chosen seed. Native `generated` descriptors remain unchanged.
The generation cache includes the complete descriptor and simulation version.
Access checks run before every request or cache reuse and before match start.
Generated blobs stay private; existing room/match membership authorizes downloads.
Completed jobs refresh only rooms still waiting on that job. Match setup preserves
release references and blob GC retains package/map bytes referenced by matches.
Verified generated-map provenance is copied when those bytes become a shared map.

`session.hello.client.generatorSharing` advertises support. Members without this
capability receive `update_required` before creating/joining scripted rooms; hosts
cannot convert rooms containing older members. Realtime delivery filters unsupported
scripted contracts. Native browsing installs only exact releases, verifies both
hashes and records provenance using the existing durable-storage rollback.

[Multiplayer index](README.md) · [Documentation index](../README.md).
