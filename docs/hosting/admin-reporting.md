# Administration and reporting

Moderation, operations recovery, activity reporting and financial metering.

## Admin reporting

`/admin` opens Overview for administrators and Reports for moderators. Unified
reports cover all six libraries; Content permits restoration without an open
report. Legacy moderation URLs remain available. Roles and deletion follow the
existing self-role and hierarchy rules. The deletion form explains consequences
and requires the current account name. Match verification filters expose pending
and failed outcomes; force is a separate confirmation that remains audited.

Operations shows uncertain studio requests, current reservations per product,
queued/failed engine jobs, queue age and last-seen worker leases/engine agents.
Inspect details to see safe metadata and available metering evidence. Returning a
generation reservation requires a reason and confirmation that delivery cannot
be recovered. AI Studio and Hive reconciliation charges measured usage up to the
original reservation, releases the remainder, and requires evidence. Inspect the
request details first, explicitly enter or copy measured token counts, and confirm
them against provider evidence. Unknown usage must not be submitted as zero. Wallet and
request locks make repeat/concurrent resolution idempotent. There are no automatic
provider retries or arbitrary credit adjustments. Audit can be filtered by actor,
action, target and dates; moderators only see moderation actions.

Set `analytics` in the instance JSON to independently control collection and
admin display (both default true):

```json
"analytics": { "collection": true, "display": true }
```

All replicas of an instance must use the same collection setting. Activity is one
conflict-safe marker per account per UTC day for a successful authenticated
non-admin request or realtime action, including activity across UTC midnight.
Guests and registered accounts are shown separately. Service credentials,
anonymous visitors, health checks and dashboard polling are excluded. Identifiable
markers are kept for 90 days, included in export and erased on account deletion;
anonymous daily totals are kept for 24 months. Daily counters update transactionally
with source changes, survive source cleanup, and handle repeated/late completions.
Opaque contribution snapshots remember only the last source state actually counted.
When a source changes after a collection pause, reconciliation applies the difference
from that snapshot
without subtracting events that were never recorded. This bookkeeping follows source
retention; anonymous daily totals remain after the source is removed.
Worker maintenance applies retention. Stopping collection creates a history gap;
it does not manufacture activity when restarted. Historical source backfills are
explicitly incomplete and never use `last_seen_at` to infer active users.

Analytics uses UTC 7/30/90-day periods with previous-period comparisons, accessible
charts/tables and CSV downloads. Status trends are cohorts by request creation day;
completion duration uses creation-to-completion time for successful deliveries
with known completion timestamps. Completion comparisons show mean seconds and
sample counts. Charts keep the selected UTC date range, show individual observations,
and break lines across unrecorded days; a gap is not a recorded zero. The current
period includes the unfinished UTC day, whereas the previous period contains
complete calendar days. Lifetime top-download rankings are independent of the
selected reporting range. Download counts follow
library counting rules and are not unique users; skin download history is unavailable.
All admin responses disable public caching; aggregate endpoints cache internally
for 60 seconds and display their updated time.

Finances defaults to live payments. Verified payment paths write an idempotent
reporting journal of actual monetary amounts; currencies and live/test/unclassified
modes stay separate. Locally verified historical credit purchases retain their
original pack amounts with unknown mode. Historical skin cash amounts remain
unknown. Credit reversals are never used to infer cash refunds. Credits are shown
in separate product units, including purchased/granted/consumed/returned/reserved.
Disputes are separate from refunds. Payment, refund and dispute-opening times
come from verified provider facts. If only a closed dispute’s current state is
available, its closure is initially dated when reconciliation observes it; a
verified closure webhook replaces that observed timestamp. Fees, hosting bills,
exchange rates and profit
are outside this report. Cash is formatted using
[Stripe charge/refund currency units](https://docs.stripe.com/currencies#special-cases),
including zero-decimal currencies and the ISK/UGX API exceptions; CSV preserves
exact integer minor units. The payment-mode filter applies only to cash. Credit
flows cover the current selected UTC range across all modes; reservations are
current snapshots. Provider costs include attempts across all modes and estimates
cover only attempts with available usage and pricing; partial subtotals are labeled.

Provider attempts retain metering even when saving the result later fails. Supply
`analytics.providerRates` as an array of immutable rate versions with `version`,
`model`, `currency`, `effectiveAt`, `inputMicros`, `cachedInputMicros`, `outputMicros`
and `callMicros`. Token rates are millionths of currency per million tokens; the
fixed call rate is millionths of currency per attempt. Rates are monetary prices,
independent of customer credit rates. Measured cache-write tokens are retained,
but attempts with cache-write usage remain unpriced because this version has no
separate cache-write monetary rate. Recovery still honors existing customer credit
cache-write rates. A model uses the latest effective version at
attempt time. To change a price, add a new version; editing a persisted version
fails startup. Missing usage or pricing remains unavailable, never zero. No provider
rates are preconfigured: the operator must enter verified contract prices.

Roll out additive migrations and backend before the dependent web build. Verify
live dashboard totals against source tables using read-only queries; test destructive
actions on a disposable database. Schema rollback is unnecessary for a UI rollback;
collection/display can each be disabled independently.

[Hosting index](README.md) · [Documentation index](../README.md).
