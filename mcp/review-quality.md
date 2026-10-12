# Independent quality review and revisions

Scope: desktop settings/preferences, platform/source registration, documentation,
and the MCP harness organization. Reviewed in the isolated MCP worktree.

## Findings and resolutions

- Settings > MCP server was appended to the shared settings implementation,
  unlike other feature categories. Extracted SettingsScreenMCP.cpp, registered it
  in CLIENT_SOURCES, and kept generic screen navigation/timer integration in the
  shared implementation.
- Running/Disabled/Error/None were raw English service strings. UIService now
  supplies structured ListenerState/failureReason/hasController accessors; the
  settings screen translates its display independently of protocol metadata.
  Starting reflects asynchronous listener retirement. Five new keys have English
  values and explicit pending entries in the other 32 locales.
- The theme preference comment incorrectly described MCP fields after insertion.
  Corrected it and documented desktop automation preferences as local state.
- Tests bound fixed ports 27203/27204 and lacked [network] runner tags. The HTTP
  fixture uses an actual ephemeral bound port; service fixtures reserve an unused
  ephemeral port, then immediately configure the listener. Network-tagged cases
  serialize within the runner. The service reservation has the ordinary close/
  bind race; failed startup is bounded and reported rather than silently retried.
- Several future.get() assertions could block forever when completion regressed.
  Centralized immediate readiness assertions after deterministic dispatch/frame
  steps. Listener retirement/startup polling is bounded to two seconds.
- Preference persistence lived inside the large input/lease case. Split it out
  and added defaults, invalid enablement, and malformed/range port coverage.
- Added HTTP empty/duplicate Origin and duplicate method-header tests, plus the
  application input boundary regression: observe human input without dispatching
  queued remote work, release while unavailable, fail a partially delivered chord
  and release its held state.
- Expanded developer documentation with immutable registry initialization,
  worker-handler vs application-callback thread contracts, cancellation ownership,
  screen dispatch acknowledgement order, and listener restart status.

## Review limits

Native recording is always supported in this repository; an initial suggestion
that recording=0 native builds fail was withdrawn after reading build/runtime
configuration. Screenshot/recording checks remain as required renderer coverage.

Verification is run by the parent agent against the combined changes; this review
note itself does not claim platform builds, runtime checks, or published evidence.

## Second integrated cross-review

Re-read Protocol, HTTP sessions/retirement, ToolRegistry, UIService ownership,
ScreenStack admission/completion, and extension documentation after the second
reviewer's fixes. No remaining production blocker identified in these boundaries.
The application callback completes after updateExecution dispatches buffered input,
before rendering; corrected stale documentation that said before update. Documented
that an unavailable observation-only pump still handles inspections and releases.
UI reviewer added cleanup-prefix consumed-offset, cleanup-only interrupted-tail,
post-dispatch unavailable capture, and reentrant extension regressions. This reviewer
added exact MIME matching, q=0/invalid quality, case-insensitive content negotiation,
combined Accept fields, and duplicate Content-Type transport checks.

Extension API hardening suggested to protocol reviewer is resolved: registration
rejects an empty handler, and ToolRegistry::call rejects a null Request returned by
an extension handler instead of producing an empty HTTP success response. Integrated validation remains the parent's
responsibility; source review and diff formatting do not establish runtime coverage.

## Third-party buffered-consumer receipt pass

Independently reviewed InputDispatchReceipt, EventQueue ranged erasure, ScreenStack
index translation/pending receipts, Engine/GameSession queue transfer and cancellation,
MapEditor consumed-prefix accounting, and UIService completion snapshots/barriers.
No concrete blocker found in the settled source snapshot. Error completion marks the
Request cancelled, so retained callbacks cannot let revoked queued input run later.
Offsets retain unrelated human events and EventQueue retains UTF-8 storage after erase.
Expanded docs to explain delayed actual-consumer acknowledgement, buffered-screen
extension obligations, human-priority admission and its deadline limit, cleanup bypass,
and explicit shutdown/resize/suspension receipt resolution. Null extension handler
hardening previously suggested is now implemented by protocol reviewer.

## Narrow final acknowledgement-state review

Reviewed dispatchAcknowledged alongside finish, afterDispatch, capture completion,
queue admission resets, and the staged-release cancellation regression. No concrete
defect found. Generating a final event batch sets dispatched internally, but public
metadata reports dispatched only after the consumer receipt acknowledges that batch.
Cancelling a hidden staged release therefore reports dispatched:false and
input_started:true while retaining its undelivered cleanup. Empty release batches
can still acknowledge immediately because no held input remains to dispatch.
Input capture_after starts only after final acknowledgement, so its capture failure
preserves dispatched:true. Screenshot-only calls continue to report dispatched:false.
Interrupted later cleanup does not erase an already acknowledged input operation.
This is a source review; parent owns verification against the final built objects.
