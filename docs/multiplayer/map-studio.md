# AI Map Studio

Map authoring jobs, credit reservations, delivery and recovery contracts.


The optional map studio lives at `/map-studio` on the online app host, reached
through **Build in AI Map Studio** in the Maps library. The persistent main sidebar groups
the studio under Maps. The public
static website can link into it; it does not hold accounts, credits or authoring
state. The maintained image-authoring modules were ported from the separate
`Globulation2/glob2-ai-map-generation` prototype (GPL-3.0-or-later, originally
extracted from Glob2 commit `1888710f5e948013e0b0859c61a6f1588144a213`).

`packages/billing` shares credit arithmetic and hosted-checkout mechanics while
preserving Hive's durable namespace and adding separate map wallets/purchases.
`packages/map-studio` owns transactional threads, messages, generation snapshots
and delivery. `packages/engine` shares the bounded native adapter and blob I/O
between the engine agent and authoring worker. `apps/ai-map-worker` journals provider attempts and runs bounded
Python reference/crop processes. Native conversion is an `import-ai-map` engine
job routed by simulation version. Its map, preview, categorical export and report
are private blobs. Provider keys never reach Python or engine subprocesses.

REST under `/api/v1/map-studio` provides account state, thread creation/listing,
messages, conversation turns, legacy explicit generation, checkout, per-request progress and authorized stage
images. Thread creation accepts an optional client UUID; retrying the same owner,
UUID and title returns the original project. Clients persist this UUID and the
first message request ID before sending so an unknown HTTP outcome does not
create a duplicate project. The browser reads a repeatable-read thread snapshot
with an event cursor, then opens `/threads/:id/events` as a persistent SSE stream. Events and their
state changes commit together; a locked per-thread counter preserves commit
order. Postgres notifications wake readers, while reconnect listeners and
15-second heartbeat catch-up recover missed notifications. `Last-Event-ID`
resumes delivery; clients deduplicate cursors. Session authorization is checked
on every catch-up, and slow connections close instead of buffering indefinitely.
The old long-poll route remains available for older clients.

Stage, artifact and check events retain the creation journey with the owning
thread. Image descriptors reference owner-authorized routes, never arbitrary
blob hashes. Older requests recover only recorded images and delivery summaries
from a safe checkpoint allowlist; missing historical checks are not invented.
Events and descriptors participate in account export and cascade on deletion.
The workspace defaults the shared sidebar to a collapsed rail and gives conversation
and canvas equal, resizable full-height panes. Settings and the accumulated brief
live beside the anchored composer; build cards appear in the conversation. A compact
canvas status and an expandable Build details inspector retain live stages, images,
playability checks and history. Mobile Chat/Map tabs preserve drafts and scrolling.
Selecting an older delivered version makes it the visible editing target; changing
settings starts a fresh map. Preparing a failed-build retry only fills and focuses
the composer; sending it is a new turn, never an automatic repair.
The separate no-credit landing page preserves draft writing and access to saved
projects; active last-credit generations open their workspace. Drafts, pending
submission identities and revision settings survive same-tab refresh and checkout.
Payment-return URLs trigger wallet refresh without granting credits themselves.
Messages cost no map credits but require an available
map credit. Sending to `/threads/:id/turns` authorizes at most one build and snapshots
text, settings and optional parent context. The worker returns a validated `discuss`
or `build` decision with its reply and updated brief. Questions, brainstorming and
material ambiguity remain discussion; concrete creation and edit requests can build.
Completing a build-directed turn atomically saves the reply/brief, completes the chat,
enqueues one generation with a persisted identity and `sourceTurnId`, and reserves
one credit under the wallet lock. Reloads, lost responses and worker retries cannot
enqueue another build. The browser follows events and never enqueues from them.
Legacy `/messages` requests remain discussion-only and `/generate` stays available
for older clients. Deploy the updated workers before the API and browser so every
new turn is handled by a worker that understands build decisions. A generation reserves one credit; a successful validated
delivery consumes it and failures return it. Each request snapshots the rolling
conversation and accumulated design brief, settings, parent version and pipeline
version. A parent revision retains its dimensions/player count; changing these
starts a fresh map in the same thread. Previous versions remain immutable; edited downloads must be uploaded as a new
catalog map instead of replacing an AI artifact.

Delivered versions become independent private catalog entries. Publishing changes
only the selected map's visibility; conversation and sibling drafts stay private.
The studio can create a link-only room from a selected catalog version and send
the player to browser play with its invite. Existing room/match authorization
permits participants to fetch the map without accessing the authoring thread.
Private previews are served by catalog authorization, not public blob metadata.

The account’s **Download my data** export includes its Studio threads, messages,
revision inputs and checkpoints, provider attempts, and separate map-credit wallet,
ledger, purchases and usage. It includes only the owner’s data and omits internal
worker lease credentials. Catalog exports also include map authoring metadata.
Account deletion removes private Studio history and import jobs and releases
unfinished generation reservations. `studio_provider_usage` retains only daily
UTC call totals, maintained by a journal-insert trigger, so deleting projects
cannot replenish the service's provider-call budget. These totals contain no
account identity or conversation data; financial ledger and purchase records
remain separate from the deleted authoring history.

The supported envelope is independent 128/256/512-cell sides and 2–8 colonies.
The post-import native report gates valid starts, walking connectivity, nearby
wheat/timber, buildable ground and fertile grass. These are minimum opening checks,
not proof of competitive balance, long-term economy or human enjoyment. Qualify
model outputs with modern-AI games, sustained growth checks and human play review
before enabling sales. See the hosting guide for flags, credentials and recovery.

[Multiplayer index](README.md) · [Documentation index](../README.md).
