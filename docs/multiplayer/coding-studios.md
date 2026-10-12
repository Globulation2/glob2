# AI and generator coding Studios

Interactive coding tools, isolated validation, credit accounting and recovery.

## AI coding Studio

The opt-in `aiStudio` service provides private, single-file JavaScript projects at
`/ai-studio`. Chat edits, restores, imports and saved manual edits retain immutable
source revisions. Every model request names its expected revision and credit cap;
concurrent edits are rejected while that request is active. Replies stream through
durable, cursor-based polling events. Only a complete replacement updates code.
The assistant has no execution tools: checks, playtests and further repair prompts
are explicit user actions. New projects use the profile-2 starter; imported files
retain their API profile.

`apps/api/src/coding-studio` owns the shared project, request dispatch, provider
transport and provider-result recovery implementation. The `ai-studio` exports
preserve existing callers; explicit domain adapters provide prompts, replacement
encoding, hashing, checks and publication.
Each API replica dispatches one model request at a time per Studio domain.
Its SQL tables use `ai_studio_`, including a separate wallet, ledger, calls and
purchases. Billing shares the Hive/Map accounting implementation through an explicit
product registry. Reservations precede dispatch; known usage settles once, even if
generated source is invalid. Unknown outcomes retain their reservation and are
never redispatched. A journaled provider result can finish after a process restart or a transient
settlement/database failure without another model call. The project stays locked
until finalization or cancellation is recorded. A late result from an expired
request is retained as reconciliation evidence and never revives the old edit.
An administrator reconciles unknown usage at `POST /api/v1/ai-studio/reconcile`,
providing `requestId`, verified `usage` (`input`, `cachedInput`, `output`) and an
`evidence` explanation. Zero usage is appropriate only after confirming no billable
work occurred. Verified charges never exceed the original reservation; the
operator absorbs any overrun, with full measured usage and the adjustment recorded
in the ledger. Reconciliation does not apply an old edit to a newer draft.

Run checks uses the existing isolated AI validator and source-hash-bound upload
receipts; publishing uses the unchanged AI Library API. A saved project retains
its source and validation blobs until deletion. Account exports include Studio
projects, revisions, requests, events, playtests and billing. Deletion fences
request completion, removes project content, and retains financial audit records.

Live playtests use `/play/studio.html`, the browser engine's temporary-profile entry.
A version-1, same-origin message bridge verifies the parent window, run ID, revision,
source limits and the pinned two-player map hash. Source executes inside Glob2's
JavaScript runtime. Tests default to seed 19 and Numbi; Nicowar is also available.
The existing AI-only local spectator path supplies camera, pause and speed controls.
Runtime diagnostics and results are bounded and belong to the tested revision;
editing a draft never changes an ongoing match. Browser results cannot authorize
publication. Closing the workspace ends its local test.

The Studio parent document requires COOP/COEP headers, so crossing into or out of
its route performs a document navigation. Only the dedicated game entry permits
same-origin framing; ordinary game, account and admin pages remain unframeable.
Monaco worker responses also carry the parent's COEP policy.
Monaco and its language workers load on desktop only; narrow screens retain a plain
text editor. Local unsaved-draft recovery is scoped to account and project.

Enable only after a maintainer playthrough. Configure `aiStudio.enabled`, `model`,
`rate` (version and integer credits per million input/cached-input/output tokens),
`maxRequestCredits`, and `maxOutputTokens` in the instance config, plus
`AI_STUDIO_OPENAI_API_KEY`. Credit sales additionally need `salesEnabled`, configured
`packs`, `AI_STUDIO_STRIPE_SECRET_KEY`, and `AI_STUDIO_STRIPE_WEBHOOK_SECRET`; the
Stripe callback is `/api/v1/ai-studio/stripe`. Keep rates and caps aligned with the
selected model; changing a rate does not change a previously reserved call.

Studio checkout submissions carry a client-generated purchase UUID. Retrying a lost
response reuses that purchase, its captured credit pack and its Stripe idempotency
key. Returning from checkout starts a new attempt for a subsequent purchase.
Known open checkout sessions are retrieved; unknown outcomes older than 23 hours
require reconciliation rather than redispatch, because Stripe may expire
[idempotency keys after 24 hours](https://docs.stripe.com/api/idempotent_requests).
The existing Hive and Map checkout callers retain their separate products and
balances.

Monitor `ai_studio_requests` status/lease ages, `ai_studio_calls` with `uncertain`
status, validation queue age, and browser launch errors. Studio request failures
are structured API logs. Playtest summaries are client-reported development data,
not competitive ratings or trusted match results. V1 has no multiplayer custom
controllers, remote live games, community remixing, subscriptions, or shared credits.


## Generator coding Studio

`/generator-studio` and `/api/v1/generator-studio` use the shared coding Studio
lifecycle with a separate opt-in `generatorStudio` configuration and billing
product. Migration `0058_generator_studio.sql` adds independent `generator_studio_`
projects, immutable two-file revisions, requests, events, checks, runs and financial
tables. Revisions persist a JSON envelope of manifest text and script text;
invalid manifest JSON is preserved for repair. A completed `replace_generator`
response must contain both full files and pass the expected-revision check before
one atomic revision is applied. Incomplete, malformed or ambiguous replacements
retain usage accounting without changing either file. Uncertain provider outcomes
are never automatically dispatched again; journaled results recover through the
same settlement path as AI Studio.

The generator adapter supplies committed documentation, toolkit declarations,
starter source, current files, bounded conversation and explicitly supplied
diagnostics. The assistant has no execution tools. Separate declaration leases
and model paths provide Monaco completion for each domain, with plain text editing
on narrow screens. Shared recovery, conflict handling, historical inspection,
restoration and generated-edit Undo operate on both files together.

Explicit local runs retain revision, draft hash, settings and bounded client
summaries. `/play/generator-studio.html` uses the same temporary-profile host and
validated, same-origin, versioned run envelope as AI Studio. Native package parsing,
control validation, generation scheduling and interpreter budgets remain
responsible for execution. The engine reports package hash, engine/simulation
version, generation duration, diagnostic and bounded telemetry. The frozen world
is rendered through MapPreview, then optionally launched with Nicowar colonies
from its serialized snapshot. Replacing a host discards late results. Serial
browser builds retain the existing bounded synchronous generation fallback;
threaded builds generate on a worker.

Checks submit the exact saved package through isolated Generator Library validation
and retain the upload receipt with the project revision. Publish remains the
existing Generator Library API and requires a valid matching unexpired receipt;
local browser reports cannot authorize it. Account export includes all private
project files, revisions, requests, events, checks and runs. Account deletion fences
active completion and removes private content while retaining financial audit
records. Financial reporting and reconciliation use the separate generator product.

Deploy the additive migration and backend before the web/browser entries. Keep
`generatorStudio.enabled` false until a maintainer playthrough. Its model, rate,
request cap, output cap, sales and pack settings follow `aiStudio` independently;
credentials are `GENERATOR_STUDIO_OPENAI_API_KEY`,
`GENERATOR_STUDIO_STRIPE_SECRET_KEY` and `GENERATOR_STUDIO_STRIPE_WEBHOOK_SECRET`.
Stripe callbacks and administrator reconciliation use
`/api/v1/generator-studio/stripe` and `/api/v1/generator-studio/reconcile`.
Generator Studio balances are never shared with other products. V1 excludes
multiple-module editing, automatic repair loops, ranked integration and remote
trusted playtests.

[Multiplayer index](README.md) · [Documentation index](../README.md).
