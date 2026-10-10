# Deploy terrain and building Studios

Optional asset-authoring services, deployment flags and validation.

## AI Terrain Studio

Terrain Studio is optional and disabled when `terrainStudio` is absent. Apply
platform migrations before starting the API or its worker. Configure:

```yaml
terrainStudio:
  enabled: true
  salesEnabled: false
  textModel: <structured-output text model>
  imageModel: <image model supporting transparent PNG generation and edits>
  pipelineVersion: terrain-v1
  providerCallsPerDay: 200
  chatPerHour: 60
  maxOutputTokens: 16000
  timeoutSeconds: 1800
```

Use a model supporting the configured image options described in the
[OpenAI image API](https://developers.openai.com/api/reference/resources/images/methods/edit).
Set `TERRAIN_OPENAI_API_KEY` only on the authoring worker; `deploy/.env.example`
lists its credentials and optional pinned image. Start the worker with
`docker compose --profile ai-terrain up -d --build ai-terrain-worker`.
The `ai-terrain-worker` image shares the matching
native engine, includes the artwork converter and pins Pillow through
`tools/asset-requirements.txt`. Local workers additionally require `ENGINE_BINARY`,
`GLOB2_SOURCE_DIR`, and `TERRAIN_PYTHON`; use the pinned asset encoder interpreter.
Start with `npm run start --workspace @glob2/ai-terrain-worker` in `platform/`.

Terrain credits have independent wallets. Configure `packs`, enable
`salesEnabled`, and set `TERRAIN_STRIPE_SECRET_KEY` and
`TERRAIN_STRIPE_WEBHOOK_SECRET` on the API for sales. Expose the signed webhook
`/api/v1/terrain-studio/stripe`. A build reserves one credit and settles only
when its exact validated package is saved privately. Provider-call limits pause
work until capacity becomes available; they include anonymous daily totals so
account deletion cannot reset service capacity.

Authoring workers share the API's blob volume. Compose explicitly sets
`BLOB_DIR=/var/lib/glob2/blobs` for these services; engine-derived worker images
otherwise fall back to a relative directory on their read-only root filesystem.

Requests and provider stages are durable, with leases and heartbeats. Completed
stages are reused after restarts. Ambiguous provider outcomes retain the reservation
and require operator reconciliation; they never dispatch a duplicate automatically.
After confirming a request cannot be recovered, an administrator can use
`POST /api/v1/admin/terrain-studio/requests/<id>/fail` to return its reservation.
Cancellation waits for an in-flight provider outcome. Ordinary set or draft
deletion rejects active Terrain Studio work so its reservation and provider journal
remain available; finish, cancel, or reconcile the request before deleting.
Account export includes
project and request history, artifact metadata and hashes, reports, and candidate
package documents. It does not bundle uploaded references or source-image bytes;
download those through the project's private artifact links before deletion.
Account deletion removes private project records and their blob references;
unreferenced bytes are subsequently collected under the normal blob-GC grace
period. Financial ledger history follows existing retention policy.

Serve `/api/v1/terrain-studio/threads/<id>/events` as an unbuffered authenticated
SSE stream, as for Map Studio. Keep the matching browser game runtime deployed:
the scene preview uses the optional `map validate-set --gallery 1` renderer path.
Existing packages, save formats, and simulation rules are unchanged by this studio.

### AI Building Studio deployment

AI building authoring is optional and independent of the manual building library.
Migrate the database before enabling its API or worker. Use the `ai-building`
Compose profile and `ai-building-worker` Docker target; its engine and stock
building definitions must come from the same build. The worker requires
`ENGINE_BINARY`, `GLOB2_SOURCE_DIR` (including building-catalog documentation and
stock definitions) and `BUILDING_OPENAI_API_KEY`.
The API and authoring worker must use the same blob store. Compose sets
`BLOB_STORE=fs` and `BLOB_DIR=/var/lib/glob2/blobs` to match their shared volume;
local workers must also point `BLOB_DIR` at the API's blob directory.
Artwork generation also needs stock camera sprites: the Docker target supplies
them under `studio/building-references/`, while source checkouts use `data/gfx/`.
A properties-only edit does not require these image references.
Configure the instance:

```yaml
buildingStudio:
  enabled: true
  salesEnabled: false
  textModel: <configured text model>
  imageModel: <configured image model with transparent PNG support>
  pipelineVersion: building-v1
  providerCallsPerDay: 100
  chatPerHour: 60
  maxOutputTokens: 16000
  timeoutSeconds: 1800
```

Model names and service budgets are operator choices. Generation uses a separate
building wallet: one credit per validated delivery; discussion and restoration
are free. Enable sales with the existing credit-pack structure and
`BUILDING_STRIPE_SECRET_KEY` / `BUILDING_STRIPE_WEBHOOK_SECRET`; the signed webhook
is `/api/v1/ai-building-studio/stripe`. Disabling generation retains access to saved
projects and candidates. An administrator can reconcile an unrecoverable uncertain
request through `POST /api/v1/admin/ai-building-studio/requests/:id/fail`, returning
its reservation and recording an audit entry. Inspect provider records before
reconciliation; never redispatch an ambiguous paid call automatically.

Validate a configured provider before enabling sales. The opt-in worker test
requires a disposable PostgreSQL test database, `BUILDING_OPENAI_API_KEY`,
`BUILDING_LIVE_TEXT_MODEL`, `BUILDING_LIVE_IMAGE_MODEL` and
`BUILDING_NATIVE_BINARY` pointing to this revision's built engine. From `platform/`,
run:

```sh
npx vitest run apps/ai-building-worker/test/pipeline.test.ts -t 'live paid provider'
```

This makes paid model calls through the real worker pipeline. It retains the
building archive, native validation report and composited stage previews in
`artifacts/building-studio/live/`. Inspect the finished and construction images at
game scale, the requested capabilities and the construction links. A passing
mock-provider test alone does not establish provider compatibility or art quality.
The native-decoder test can also consume previously generated PNGs with
`BUILDING_VALIDATION_IMAGE` and `BUILDING_VALIDATION_SITE_IMAGE`; this checks sprite
conversion and team-color layers without making provider calls.

[Hosting index](README.md) · [Documentation index](../README.md).
