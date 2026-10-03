# Hive Mind

Hive Mind adds a natural-language commander to online matches, including ranked
matches, alongside ordinary manual controls. Players give commands, receive reports,
and manage named standing orders. Configure Hive Mind under Settings → Online. Ctrl+Enter opens the chat-style
commander input; Enter sends and closes it, and Escape keeps the draft for later.
Ctrl+Shift+Enter stops paid supervision. Both shortcuts are configurable under
Settings → Controls. A separate Stop commander button provides the same action
for mouse and touch users. Reports / details shows the ten most recent reports.
Named standing-order cards stack at the bottom-left of the
map and expose Pause, Resume and Cancel separately. The cards do not block manual
map controls outside their bounds. Settings links to the browser account and
credits page; no credit configuration appears in the gameplay HUD. Installed standing orders continue without credits. Assistance
is permitted in ranked play on instances that enable it.

The instance feature flag and sales flag default to off. Enabling the feature is
an operator decision after the release gates below; a successful build alone does
not satisfy those gates.

## Trust and execution

`platform/packages/hive` owns the commander loop, prompt versions, provider adapter,
credit accounting and payment fulfillment. The API authenticates every operation
against the live match participant and the `hive-mind` entitlement. Team identity
comes from that participant record, never a tool parameter. Registered accounts
receive access through a grant or paid purchase. Entitlement and available balance
are independent checks.

The agent uses the AI SDK through `ModelProvider`. Application code owns run leases,
tool journals and billing. A database run lease permits one active provider run per
account/match/seat even across API processes. Commands change the generation and
steer or interrupt work. Every provider call, including repairs and event-driven
follow-ups, requires a new transactional reservation. Provider retries are disabled;
uncertain outcomes retain their reservation for reconciliation.

The authenticated client bridge copies a tick-labelled `Script::Observations` view
for the assigned team. It never uses the map-script profile. A separate QuickJS
host receives only that copy. The browser uses a separate worker and WebAssembly
memory without a filesystem. Native clients launch a fresh child process with an
empty environment and a five-second deadline. Linux additionally restricts address
space, CPU time and syscalls; no filesystem lookup, network, subprocess or ptrace
syscalls are admitted. Other native OS containment and platform-specific behavior
must be verified before enabling those distributions.

No generated code executes in the relay, another player's client, or the replay
verifier. The client validates all descriptors with `Script::order`, allocates the
complete order batch, commits the local checkpoint, then splices the batch into the
ordinary human order queue. Manual and automated orders share its sequencing.
Acceptance means queued, not successful gameplay execution; normal placement,
resource and simulation rules still apply.

## Version 1 contract

The complete supported contract is packaged in
[`commander-api.txt`](../../platform/packages/hive/src/commander-api.txt), with
[player-only declarations](../../examples/hive-mind/hive.d.ts) and
[executable examples](../../examples/hive-mind). The prompt includes that contract
and copies of those examples; tests check they match. Do not import the broader
map scripting declarations into an agent prompt.

Each source declares synchronous `step(ctx)` returning `{output?, orders?}`.
Successful invocations preserve ordinary top-level data. `ctx.wakeAgent` emits a
bounded structured event; it cannot directly call a model. Entity references need
both ID and generation, and all other-team observations obey current visibility.
Allies do not grant access to their private state. Explored terrain can be remembered;
that observation's tick must not be confused with current visibility.

Limits: 128 KiB source, 1 MiB encoded global state, 64 KiB output, eight standing
orders, 32 orders per invocation, and 64 pending commander/manual orders at the
submission boundary. The interpreter also enforces its existing deterministic
work and memory budgets. Standing orders use integer intervals of at least 25
simulation ticks. Due programs execute in stable ID order without overlapping;
missed intervals do not produce catch-up bursts. Replacement is preflighted with
no effects and requires the expected revision. Failed replacement leaves the old
program intact. Replacement resets globals unless a validated migration is supplied.

Triggers require authorization on the originating standing order to start new paid work.
A bounded command does not revoke existing supervision. Stop revokes supervision
for existing programs and already-dispatched installations. The Settings default
applies only to new commands; it does not cancel existing orders. The server deduplicates
by program revision, key and 25-tick window, limits wake-ups to six per minute,
and coalesces pending context into the active run. Alerts remain reports when
credits are exhausted. Stale alerts are not replayed after a top-up.

## Recovery

Operation identifiers, server results, program definitions and run generations live
in PostgreSQL. The client keeps program globals, cadence and a dispatch uncertainty
marker in its online storage, outside save-game/simulation state. Match IDs isolate
programs across games. Execution waits for catch-up and a renewable, exclusive
client lease. Expired dispatches become uncertain, not pending for redelivery.

Result delivery retries the same operation without executing it again. If an order
may have been submitted but completion cannot be proved, the affected automation
pauses for review. A client restart after gameplay dispatch is deliberately
conservative: it does not infer execution from queue submission. A replacement
client without a matching local checkpoint restores paused metadata cards using
the current server revision. It can remove or explicitly replace the order, but
cannot resume it or migrate missing globals. Lease deadlines are measured from
the poll request start, so delayed responses cannot extend an old client lease.
Worker failures pause automation without terminating the match.

## Credits and purchases

Configure `hiveMind` in the instance YAML. An enabled instance needs `model` and a
versioned `rate` containing integer credit costs per million input, cached-input
and output tokens. Output usage includes billable reasoning exactly once. Pack
configuration contains `id`, Stripe `priceId`, integer `credits`, `amount` in minor
currency units and `currency` (USD, CAD, EUR or GBP in lowercase). Pack prices must
match Stripe's retrieved checkout totals. There are no subscriptions, automatic
top-ups, expiry or per-match spending caps.

Only the API receives `HIVE_OPENAI_API_KEY`, `HIVE_STRIPE_SECRET_KEY` and
`HIVE_STRIPE_WEBHOOK_SECRET`. Never put provider credentials in the client, instance
public metadata, test fixtures or logs. Use dedicated production credentials.
`HIVE_DEVELOPMENT_CREDITS=1` enables the operator-only development grant command:

```sh
cd platform
DATABASE_URL=... HIVE_DEVELOPMENT_CREDITS=1 node packages/hive/scripts/admin.ts grant ACCOUNT_UUID IDEMPOTENCY_KEY 100000
```

Reservations lock the account wallet, preventing concurrent spending of the same
balance. Settlement records actual usage with the call's original rate card and
releases unused funds. An operator can settle an uncertain call after obtaining
provider usage evidence; zero usage requires evidence the provider did not bill it:

```sh
DATABASE_URL=... node packages/hive/scripts/admin.ts settle ACCOUNT_UUID CALL_UUID INPUT_TOKENS CACHED_INPUT_TOKENS OUTPUT_TOKENS EVIDENCE_REFERENCE
```

Stripe-hosted checkout opens in the browser. The return page only polls account
status. Configure signed webhooks at `/api/v1/hive/stripe` for checkout completion,
asynchronous payment success, refunds, and dispute creation/closure. Fulfillment
retrieves current Stripe state and is idempotent. Per-payment locks serialize
canonical reads and ledger writes. Refund/dispute events arriving first retrieve
the checkout before applying the reversal. Spent refunded credits may make a
balance negative, preventing further calls until sufficient credits are added.

## Validation and release gates

Run platform checks from `platform/` and engine tests via the shared registry:

```sh
npm run check
scons release=1 server=0 tests
python3 test/run_tests.py --filter 'HiveMind*/*'
```

Contract tests exercise copied visibility, stale references, altered team IDs,
forbidden queries, work/output limits, persistent state, migrations and executable
examples. Server tests cover permissions, leases, run exclusivity, reservations,
idempotent settlement, purchase duplication and reversals. Continue to run the
existing JavaScript visibility and save/replay compatibility suites when changing
the shared interpreter or observation implementation.

Production enablement requires held-out live-model evaluations: at least 90%
first-attempt validity, 95% bounded objective completion and no known permission
failures. Record model, prompt, API and rate versions, repair rate, latency and cost.
Select the lowest-cost qualifying candidate, breaking ties by latency. Pin the
chosen model configuration; do not expose a model picker.

Also require browser and native replay checksum comparisons for identical orders,
legacy save-loading coverage, lease/reconnect load tests, verified payment test-mode
webhooks, and a real playtest of reports, pacing and manual/standing-order interaction.
Roll out with development credits, then a limited production cohort, then general
availability. Keep sales disabled until dedicated credentials and real prices are
configured. Missing platform or live-play evidence is a release blocker, not a
passing result.

Run the held-out evaluation only with explicit development opt-in:

```sh
HIVE_LIVE_EVAL=1 HIVE_EVAL_REASONINGCANVAS=1 node platform/packages/hive/eval/run.ts
node platform/packages/hive/eval/select.ts artifacts/hive/eval
```

The optional `HIVE_EVAL_REASONINGCANVAS` switch reads the development environment
from `~/reasoningcanvas/server/.env.local` in the evaluation process only. It never
writes those credentials or passes them to the native game fixture. Alternatively
supply `HIVE_OPENAI_API_KEY` directly. Results, generated programs and selection
summaries go under `artifacts/hive/eval`; use `HIVE_EVAL_OUTPUT` for a fresh run.
Version 2 evaluates the production Commander and database journals with the real
client scheduler and order queue inside the native fixture. It records validity
for every generated program and commands completed without repair separately.
Recurring scenarios include manual interference; trigger conditions become true
after installation and must cause a subsequent provider call and verified action.
The selector rejects legacy smoke results. Native objective checks inspect game
state after validated orders, not the model's own success claim. Construction cases deliberately ask to establish sites, not finish
buildings. Longer economic objectives still require live playtests.

The example instance uses `gpt-6-luna` for development. The earlier version-1
smoke result does not establish production model selection; production selection
requires passing the current stateful suite. The provider
currently documents that identifier rather than a dated snapshot; retain the
returned provider model and response IDs in journals and rerun evaluation when
updating the provider, prompt or API. Cache writes can have a separate rate; when
omitted they use the published input rate. Evaluation selection records cost
bounds when provider cache-write detail is unavailable.

The account’s **Download my data** export includes its Hive credit wallet, ledger,
purchases, provider usage, sessions, events, operations and standing programs.
Rows are restricted to the account that owns the session. Internal client, lease
and run capabilities are excluded.
