# Deploy AI Map Studio

Optional paid map-authoring service setup and recovery.

## AI Map Studio

AI Map Studio is an authoring service on the app host. It has a separate prepaid
map wallet; a delivered map or revision costs one map credit. Discussion is
included, subject to `chatPerHour` and the shared `providerCallsPerDay` operator
budget. These credits cannot fund Hive Mind. Only registered accounts use it.

Configure `mapStudio` in the instance file with `enabled`, `salesEnabled`, pinned
`textModel` and `imageModel`, `pipelineVersion`, a positive daily provider-call
budget, and one-time Stripe packs. Leave both flags false until real provider
quality/cost qualification and Stripe test-mode checkout/webhook verification
pass. No production prices are supplied by the repository. The daily call budget
bounds calls, not a currency amount; also configure a provider project spend cap.

The API uses `MAP_STRIPE_SECRET_KEY` and `MAP_STRIPE_WEBHOOK_SECRET`; the optional
worker alone uses `MAP_OPENAI_API_KEY`. Run the Compose profile with
`docker compose --profile ai-maps up -d --build`. The worker image contains Python,
Pillow, native assets, generator descriptions and a matching engine binary. Its
sources and binary must come from the same engine build context. Existing engine
agents consume `import-ai-map` jobs; the platform worker applies their results.

Expose the signed Stripe webhook at `/api/v1/map-studio/stripe`. Checkout return
pages only refresh account state. Credit fulfillment follows authoritative paid
sessions and handles delayed payments, refunds and disputes idempotently.

Generation requests are a durable database queue. One request per account may be
active. Reservations and request creation are transactional; delivery, catalog
registration and charging are transactional too. Provider attempts are journaled
before dispatch. Deterministic stages resume from private content-addressed
checkpoints. Unknown provider outcomes become `uncertain`, hold the credit, and
block further requests for that account until reconciled. Administrators can
mark a confirmed unrecoverable request failed using
`POST /api/v1/admin/map-studio/requests/<id>/fail`; this releases the credit and
records an audit entry. Never re-dispatch an uncertain paid provider request.

Deploy the additive studio-event migration first, then replace and drain all old
authoring workers before updating the API and web client. The migration backfills
anonymous daily provider-call totals and counts subsequent journal inserts
transactionally, including calls from old workers. Updated workers enforce their
budget against these totals; old workers still count private journal rows, which
the updated API removes on account deletion. Do not enable the updated deletion
path while old workers remain. Keep the previous long-poll route during client rollout.
The stage event journal and authorized artifact records are retained with each
thread; do not prune their cursor history independently of the thread.

Studio progress uses persistent SSE at
`/api/v1/map-studio/threads/<id>/events`. Preserve `text/event-stream`,
`Cache-Control: no-cache, no-transform`, and `Last-Event-ID` through the edge.
Caddy's reverse proxy flushes SSE responses as they arrive; any additional load
balancer must disable buffering for this content type and permit connections
with 15-second heartbeat intervals. Verify that an authenticated `curl -N` through
the public edge receives the initial comment immediately, then stage events
before generation finishes. Reconnect with the last event ID to check replay.
Streams do not own worker lifetimes; disconnecting never cancels generation.
Stream logs include delivered-event counts, cursors, connection duration and
closure errors, without message text or provider diagnostics. Event timestamps
provide stage duration and delivery latency evidence.

Monitor `studio_requests` status/age, `studio_attempts` usage/model, map-wallet
reservations, delivery failure rates and queue age. Pause sales or generation via
the instance flags; retain blobs and payment journals during rollback. Migrations
are additive and preserve existing Hive tables and historical purchase IDs.

[Hosting index](README.md) · [Documentation index](../README.md).
