# Community music and AI Music Studio

Community release processing and optional music generation.

## Community music

`@glob2/music` owns release views, source retention and streaming ZIP export;
`protocol/src/music.ts` defines portable JSON schemas and generated fixtures.
`apps/api/src/music/routes.ts` exposes `/api/v1/music` for catalogue search,
creator drafts, uploads, inspection/conversion/publication, likes, reports and
individual/set/bulk downloads. Browsing, playback and downloads are anonymous;
publishing and reversible likes require registered accounts. Likes are the
rating, with one row per account/release. Search covers title, artist, description
and tags, with tag, licence, AI and duration filters. Pagination orders by score,
creation time and UUID; clients retain selected UUIDs across result pages.

Processing state changes and Graphile job insertion commit in one transaction.
Transient failures retain source references for up to three attempts; terminal
state is committed before source cleanup. The sweeper protects active draft
references while collecting abandoned uploads after 24 hours without activity.

`apps/music-worker` independently consumes `music-inspect` and `music-convert`
Graphile jobs. It never leases simulation-version engine jobs. It stages sources
from the private `music-uploads/` namespace, invokes `glob2music.community`, then
stores final audio, cover, waveform summaries and ZIPs in the content-addressed
blob store. PostgreSQL holds release metadata, processing state, likes, reports
and `music_assets` references. Response envelopes also include waveform peaks.
Original uploads and PCM are deleted after conversion, cancellation or terminal
failure; the scheduler expires abandoned uploads after 24 hours and sweeps orphan
source blobs. A release advisory lock and conditional state transitions make
repeated job delivery safe, including cancellation during conversion.

Published content and embedded tags are immutable. Creators withdraw releases;
moderators hide/unhide them through the Music administration tab and resolve
reports with an audit entry. Unavailable releases are removed from public lists
and every media/download route. Their previously installed copies remain local.
Private previews and public files pass through release visibility checks instead
of the generic public blob endpoint. Music asset references protect retained
files from blob garbage collection.

The React catalogue and dedicated game screens share Calm/Building/Combat labels,
waveform colors, preview controls and primary action placement. The website's
AudioWorklet receives bounded mixed PCM from a WASM build of the native preview
code. See [music pipeline](../assets/music-packages.md#community-releases) for the
self-contained file format and [hosting](../hosting/music.md#music-worker) for
operational limits.


## AI Music Studio

`/music-studio` provides CPU-only conversational soundtrack authoring, reached
through **Build in AI Music Studio** in the Music library. The persistent main sidebar groups
the studio under Music. REST under
`/api/v1/music-studio` owns account state, projects, durable turns, legacy messages/generation,
cancellation, private artifacts and checkout. `packages/music-studio` owns the
transactional journal and delivery; `apps/ai-music-worker` owns provider calls,
bounded agent tools and isolated Python execution. The score/rendering contracts
are described in the [music pipeline](../assets/music-packages.md#online-ai-music-studio).
Discussion and composition calls use distinct strict JSON response schemas
through the provider's response format; the worker also validates discussion
fields and tool actions before using them. A prompt alone does not establish
that transport contract. Only the final answer message is consumed; commentary
and intermediate JSON messages are not concatenated into composer actions.
Each source write schedules trusted score validation and rendering automatically;
the next model call receives the resulting checks to repair any failures.
This prevents repeated source rewrites from consuming the action budget before
a candidate is ever validated. The three-render limit still applies.
Common project/message and credit-pack schemas live in `protocol/src/studioCommon.ts`;
studio-specific settings, products and balances remain separate.

The web client submits `MusicStudioTurn` to `POST /api/v1/music-studio/threads/:id/turns`.
A turn freezes its UUID, text, settings and optional parent version. The strict
provider response chooses `discuss` for questions, brainstorming and clarification,
or `build` for an explicit creation/edit request. Completing a build turn and
reserving/enqueueing its one generation commit in the same wallet-locked transaction.
The generation UUID is persisted with the turn before dispatch; retries and replayed
completions cannot create a second generation. Browser events only refresh saved
state. Legacy `/messages` and `/generate` retain their contracts and queued work.
Deploy migration `0053_studio_draft_history.sql`, additive API support and the
updated workers before deploying the updated web client.

Music uses its own wallets, ledger and Stripe purchases. A generation reserves one
credit; creating its private immutable music release and consuming the credit
commit together. Failure or cancellation returns the reservation once. Unknown
provider outcomes keep the reservation pending for administrative reconciliation
at `POST /api/v1/admin/music-studio/requests/:id/fail`. Provider attempts are
journalled before dispatch and anonymous daily usage survives history deletion.
Known rejections are replayed as failures without another provider call. Completed
provider results survive worker recovery; only outcomes that cannot be established
from the journal require reconciliation. Render cycles are recorded before execution,
and completed candidates retain reports plus content-addressed score and audio references.
A worker restart consumes an interrupted cycle or resumes a completed candidate;
it does not silently reset the three-cycle budget.

Both studios use `apps/api/src/http/studioEvents.ts` for ordered SSE replay and
reconnect/authentication behavior, with separate notification channels. Stage,
check, candidate and composer-progress events remain with each request. History
snapshots omit worker-only source/configuration checkpoints; account exports
include them without lease credentials. Account deletion fences workers and
returns unfinished reservations before removing authoring history.

Delivered revisions use existing music-library authorization and playback.
Publishing requires confirmation of the license selected before generation; it
exposes only that release and final check results. License and attribution are
embedded in Opus tags before validation, preserving the native three-file ZIP
format. Uploaded audio cannot replace generated revisions. Withdrawing a release
uses existing library controls; it does not reveal private authoring artifacts.

Deleting a Music Studio conversation removes its private source, messages, candidates
and event history; finished releases remain independently managed in the music
library. Free-chat usage is retained as zero-credit ledger entries and anonymous
daily provider counters survive history/account deletion. Private pipeline source
fingerprints cover worker tools, Python sources and dependency/asset definitions,
and fence recovery across an incompatible worker upgrade. Successful history deletion
also clears that conversation's browser draft and checkout-return state; other projects
are retained.

[Multiplayer index](README.md) · [Documentation index](../README.md).
