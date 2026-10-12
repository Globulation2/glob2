# JavaScript AI library

Publishing and validating community JavaScript AI implementations.


`/api/v1/ais` provides a versioned catalogue for local-play JavaScript controllers.
The website owns publishing; the native settings library discovers, downloads and
installs exact releases. The contracts live in `packages/protocol/src/ais.ts`.

| Route | Behaviour |
| --- | --- |
| `GET /api/v1/ais` | `q`, comma-separated `tags`, `sort=likes\|newest\|updated\|downloads`, `favourites=true`, `owner=me`, keyset `cursor` and bounded `limit` |
| `GET /api/v1/ais/{id}` | AI identity, versions and historical validation evidence |
| `POST /api/v1/ai-uploads` | Private staged UTF-8 source bytes; queues/coalesces validation |
| `GET /api/v1/ai-uploads/{id}` | Owner-only persistent checklist and retryable failure |
| `POST /api/v1/ais` / `POST /api/v1/ais/{id}/versions` | Atomically consume a passing upload into a new AI or owned release |
| `PATCH /api/v1/ais/{id}` | Owner edits catalogue metadata and visibility |
| `DELETE /api/v1/ais/{id}` | Owner removes a catalogue AI |
| `GET /api/v1/ais/{id}/versions/{versionId}/file` | Exact bytes, safe `.js` attachment, daily deduplicated version download accounting |
| `PUT` / `DELETE /api/v1/ais/{id}/like` or `/favourite` | Idempotent registered-account social actions |
| `POST /api/v1/ais/{id}/reports` | Map-library reporting reasons and limits |
| `/api/v1/admin/ais/...` | Moderator report queue, resolution, hide and unhide |

AI identities own likes and private favourites. Immutable `ai_versions` own source
hashes, version labels, notes and download statistics. `ai_validations` bind seven
required checks to the exact hash, simulation version and validation-suite revision.
Publication locks the staged upload and parent AI; concurrent retries return the
same publication, and duplicate labels or source within an AI are rejected. Public
is the publication default; private, unlisted and moderator-hidden visibility use
the map catalogue's access conventions. Account export and deletion include AI data.

Only an active agent advertising `validate-ai` enables upload validation. Agents
probe the real engine inside Linux namespace isolation before advertising it.
The worker keeps historical reports and schedules validation for new supported
engine versions. Errors never erase a previous passing report. Expired staging
uploads and unreferenced validation blobs are reclaimed by maintenance and blob GC.
After seven days, abandoned validations without uploads or published releases are
collected, including queued jobs whose validator disappeared; active leases and
reported results are allowed to finish. Published infrastructure failures can retry
even after the original job has aged out of job history. A retry of the same source
after infrastructure failure starts another job.

Suite 1 pins two script-free fixtures (two and four players) under
`apps/engine-agent/fixtures/ais`. Each controller runs against built-in opponents
for at most 4,096 ticks, repeats from the same saved initial state, and continues
from tick 2,048. Legitimate early endings restore the final save. Full per-tick
records are compared; controller disablement is reported separately from process
exit. Changing fixtures or validation semantics requires a new suite revision.

Installation verifies the hash before startup checks and durable storage. Optional
local provenance stores origin, AI identity, version identity and hash. Explicit
updates preserve local IDs and roll back on persistence failure; saves and replays
embed source exactly as before. No local gameplay telemetry, competitive ratings,
tournament registration or multiplayer custom controllers are introduced.

[Multiplayer index](README.md) · [Documentation index](../README.md).
