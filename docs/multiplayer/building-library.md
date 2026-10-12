# Building-family library

Authoring, validation and release contracts for building families.

## Building-family authoring and releases

`platform/apps/api/src/buildings/` owns private drafts, normalized still WebP
frames, publication and public library access. `BuildingPackage` in the protocol
package describes namespaced, additive definitions; the native engine remains
the authority for catalog semantics. Draft edits use revision UUIDs for
compare-and-swap updates. Account locks serialize storage quota checks, and shared
database rate limits apply across API replicas.

A release references a content-addressed ZIP in `blobs` and a `validate-buildings`
job for one stock hash, simulation version and validation suite. Validation
constructs a bounded artwork bundle and invokes native catalog composition.
Release status comes from the job's bound result; public library queries expose
metadata and hashes, while the internal job retains the full resolved snapshot.
Only validated releases can supply runtime installation resources. Every resource
request rechecks visibility and moderation. First publication reserves namespace
ownership, and forks allocate a new namespace with internal references rewritten.
Owner metadata and visibility updates do not revalidate immutable releases;
withdrawal deletes the family, and administrative changes enter the audit log.
Maintenance retains validation jobs referenced by releases so published verdicts
survive the normal completed-job retention period.

Blob garbage collection retains every published archive. Account export includes
private draft archives, releases and social activity; account deletion removes the
account's authored families and drafts. Maps already containing those definitions
and artwork remain self-contained. See [building catalogs](../features/building-catalogs.md)
for the author workflow and format contract, and [rollout](../hosting/content-validation.md#building-family-library-rollout)
for deployment order.

AI Building Studio uses `platform/packages/building-studio/` for private
conversations, immutable candidates, revision UUID checks and building-credit
reservations. `platform/apps/ai-building-worker/` journals text/image provider
calls, assembles normalized frames, and validates complete archives through
native building composition. `/api/v1/ai-building-studio` exposes owned projects,
turns, references, progress/events, candidate downloads and explicit adoption.
There is one active request per account; completed provider stages survive worker
restarts and ambiguous dispatches require reconciliation. Candidate archives,
artifacts and base snapshots stay reachable by blob garbage collection; account
exports include studio metadata and exact candidate archives for offline recovery,
and deletion removes private journals while
retaining anonymous provider-call totals. Draft deletion is blocked during active
work so reservations and provider outcomes cannot disappear.

[Multiplayer index](README.md) · [Documentation index](../README.md).
