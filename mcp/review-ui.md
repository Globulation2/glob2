Independent UI/integration review, first round

Resolved P1: UIService injected coordinate/input events before ScreenStack processed
human actions and geometry. A human-created child caused the whole remote tail to
be discarded while afterDispatch acknowledged success. ScreenStack AdditionalInput
now prepares after human input and geometry settles, admits only when a screen can
receive it, reports the consumed prefix after the sole update, and completes before
painting. A partial remote batch returns an execution error and preserves missing
release events. Added ScreenExecution regression for order, transition deferral,
one update, and interrupted prefix.

Resolved P2: ui_release_inputs was rejected when the window was unavailable,
preventing explicit remote-owned cleanup behind native dialogs/minimization. It is
now handled before availability rejection, as ui_release_control already was.

Resolved P2: extension callbacks could revoke/reconfigure and invalidate active
before their completion dereferenced it. Keep a local shared request and complete
only if it remains active.

Organization: split observation, expiry, admission, drag and key dispatch into
named helpers; format UIService using repository style; document main-thread vs
worker ownership and temporary/human input semantics; forward-declare HttpServer
instead of exporting HTTP implementation dependencies; rename expanded renderer
lifecycle helpers pollCaptures/cancelCaptures to cover all capture kinds accurately.
Structured ListenerState/accessors enable translated settings display without
changing protocol-facing metadata strings.

Validation is coordinated by parent agent. Platform runtime omissions from the
original evidence remain unchanged until explicitly reverified.

Second-round findings resolved: listener resets used to join encoding/network
workers on the UI thread. configure now immediately closes admissions/revokes,
posts stop through HttpServer, retains workers until stopped(), and pump reaps
finished workers and starts the desired listener. Structured Starting status
covers this interval. Final shutdown joins before protocol/SDL teardown.

Hidden frames now advance queued requests without a screen admission hook: status
reports unavailable, screenshots/input fail explicitly, and release calls still
dispatch cleanup then complete. Dispatch-tail tracking includes cleanup events so
its indices exactly match ScreenStack's consumed-prefix count.

Independent protocol reviewer cross-review found cleanup releases could still be
dropped in the ordinary human phase, or in a cleanup-only partial batch with no
active request. Cleanup now stays queued until tracked additional dispatch (hidden
frames are the exception), and undelivered releases are preserved independently of
active request ownership. Remote events are never reclassified as human input.
Application-global recording shortcuts use the same additional-input interceptor;
a ScreenExecution case verifies interception counts as dispatched with one update.

Native-dialog-opening input now refreshes availability at the completion boundary.
If capture_after cannot capture there, the result keeps dispatched:true and returns
an explicit capture failure. Inspection metadata dispatched is no longer inferred
from mere absence of an error.

HTTP cross-review: io-confined posted shutdown and retained worker lifetime appear
sound; media range Accept parsing remains a minor robustness improvement suggested
to the protocol reviewer (substring matches are overly permissive). No new blocking
protocol concern identified in the second pass.

Final review found gameplay and map editor queue their handleExecutionEvent events
until updateExecution, unlike ordinary menus. Their new defersExecutionInput()
capability prevents additional remote admission during an ordinary input frame;
the human batch finishes its single update and remote work waits for the following
quiet tick. Only GameSessionScreen and MapEditorScreen own buffered Screen SDL
queues (searched Screen implementations/headers). Added a buffered ScreenExecution
probe regression showing prior human geometry is settled before remote prepare,
with one update on each tick. No simulation update/order code was changed.

Added focused MCP boundary regressions: hidden-style status/capture errors,
unavailable release, observation-only admission, deferred cleanup, cleanup-only
truncated suffix, cleanup-prefix plus temporary chord consumed offsets, changed
post-dispatch capture availability, and reentrant extension revocation.

Final independent cross-review identified an unresolved deeper dispatch boundary:
ScreenStack's delivered prefix counts calls to handleExecutionEvent, but buffered
editor/engine consumers can retain or drop events during persistence, loading, GUI
cadence, or waiting for an extracted runner scene. A true consumer receipt is needed
to complete remote requests and preserve text through actual dispatch. Parent and
protocol reviewer were informed with EngineRun.cpp paths; these findings are not
considered resolved by the quiet-frame deferral alone. UIService text ownership must
also survive any multi-frame engine queue retention.

Resolution of deeper consumer boundary (third iteration): InputDispatchReceipt is a
main-thread shared optional consumed-prefix count with cancellation predicate.
ScreenStack records queued-event indices (including application-global intercepted
events), polls actual buffered-consumer receipts, and copies completion factories
for batch/request identity. UIService blocks new work while an input receipt waits;
its completion snapshots the exact request and event batch and cannot complete a
newer request. Engine/GameSession track transferred ranges until clientStep/gui.step;
MapEdit reports its actual processed prefix, including zero during persistence and
partial progress when leaving/loading. Cancellation erases the exact pending remote
range while retaining unrelated human events and EventQueue-owned text payloads.

Cleanup has a separate admission path through buffered human frames. Front-of-queue
status/screenshot/acquire maintenance can proceed without coordinate generation,
without bypassing prior input/capture. Buffered hasPendingExecutionInput also covers
ordinary events retained across engine cadence, so remote coordinates wait until
previous human geometry has actually settled.

Hidden frames poll suspension-completed receipts without any simulation update.
Unavailable observation produces no raw input batch; release events remain queued
until an explicit fresh cleanup drain. Buffered hidden consumers clear execution
input after release routing, while only that fresh batch is completed. Old pending
receipt barriers are never reset by an unconditional completion. Delayed completion
captures only the still-live MCP service, not Application's destroyed flag members.

Regression inventory added/extended: delayed actual consumption; cancellation
preserving later human text; global interception indexing; cleanup-prefix consumed
offsets; cleanup-only partial batches; post-input capture unavailability; reentrant
extension revoke; no-event maintenance during human observation; hidden suspension
then unavailable inspection and stale callback cannot finish a newer held key;
editor persistence zero-consumed and interrupted editor loop prefix. Existing MCP
standalone pumps now explicitly acknowledge each generated event range, matching
the new production receipt barrier.

Independent Engine/GameSession final cross-review: cancellation is pruned after
append before GUI consumers, future ranges are bounds-guarded on abort, receipt
completion waits for scene readiness/GUI cadence, and suspension resolves pending
receipts. Empty receipt lists keep existing non-MCP engine behavior. No remaining
blocking finding identified. Parent owns relocated build/runtime verification and
cross-platform omission reporting. Formatting touched new/scoped blocks only;
git diff --check passes at final source freeze.

Post-freeze timing review resolved: generated DOWN events may wait in buffered
consumers, so their five-second hold expiry now starts at actual completed dispatch.
Pending, unconsumed DOWNs are protected from hold expiry, while the request's ten-
second deadline and cancellation still apply. The first consumed drag DOWN anchors
its duration; subsequent motion receipts do not restart the gesture clock. Added a
no-sleep regression manipulating timestamps to simulate delayed receipt, verify
pending holds survive, verify a 250 ms drag does not instantly finish, and verify a
held key refreshes only after dispatch. Protocol reviewer independently approved
the timing fix. No shared API change was required.

Final narrow safety fixes: expiration of an already-consumed drag hold now cancels
its active request and pending motion receipt, rather than allowing a later step
to recreate ownership without a DOWN. The queued motion is removed by the existing
consumer cancellation path and a release remains queued. A coordinate receipt also
rechecks its accepted viewport at actual consumption; geometry drift marks the
request cancelled and reports an explicit stale-viewport execution failure with
input_started metadata preserved. Added no-sleep pending-motion expiry and delayed
coordinate/resize cancellation regressions. Only UIService and MCPTest changed;
shared application/screen/engine APIs remained frozen.

Availability cancellation was tightened to exempt only explicit release operations.
Generating a key/click batch no longer exempts pending input from unavailable-UI
cancellation. The actual-consumer predicate also rechecks availability, covering a
native dialog opening in the same frame after admission. Regression tests cover
both ordinary unavailable observation and the later consumer-only availability
change, verify the pending request cancels, and verify no remote-held key or cleanup
leaks. Source changes remain limited to UIService and MCPTest; final independent
review was requested from the protocol reviewer before the parent validation run.

Final independent protocol review approved the completed narrow pass: pending
non-release input cancels at initial unavailable observation or actual consumption;
cleanup releases remain allowed, and old cancelled predicates cannot overwrite a
newer request's failure state. No unresolved findings remain from that pass. Source
and APIs are frozen for the parent's final verification.

Pinned SDL keyboard-contract review: remote KEY events now call
SDL_GetKeyFromScancode with key_event=true, honoring SDL_HINT_KEYCODE_OPTIONS and
native key-event representation. Text remains on SDL_EVENT_TEXT_INPUT. Added a
Shift chord plus Keypad 4 regression under nondefault hide_numpad options, comparing
to native key-event lookup and distinguishing character lookup; the fixture restores
the prior active hint value or removes the temporary hint when none existed.

Independent review of the protocol agent's immutable scancode-name catalog passed:
SDL name-table access occurs only during main-thread registration, captured lookup
maps are const and shared-owned, worker validation uses ASCII case normalization
and ordinary map access only, first-match duplicate-name behavior and modifier/NUL
validation are preserved. No remaining finding in this focused keyboard pass.

Cross-layer HTTP ready-error independent review passed: application failures mark
Request.cancelled to retire buffered input, but must still return their completed
JSON tool error. Session.closed continues to guard EOF/disconnect, and owner.stopping
suppresses responses from retiring listeners. Both pollReady/write run on the same
network worker, preserving disconnect/stop behavior.

Parent's runtime-driven finish correction (dispatched && !awaitingDispatch) was
reviewed: cancelled pending key batches report false, while capture_after errors
following actual consumption remain true because afterDispatch clears the receipt
barrier before capture. A rare release-only maintenance staging edge (cancel before
its cleanup drain) was reported to the parent for decision; no unsolicited changes
were made during this review pass.

Concrete staged-release metadata edge fixed narrowly: dispatchAcknowledged now
records receipt of the final input batch independently of generation/staging and
awaitingDispatch. Cancellation while a release UP sits in cleanup reports
`dispatched:false`. A later cleanup interruption while capture_after waits preserves
an already acknowledged input batch and reports `dispatched:true` for the original
input despite the capture error. Added a focused regression covering both outcomes
and cleanup/held-state removal. Only UIService and MCPTest changed; source is frozen
for final parent rebuild and independent protocol review.

Independent final dispatchAcknowledged review passed: staging cannot acknowledge a
release, actual final-input acknowledgement survives subsequent capture/cleanup
failure, lifecycle resets are complete, and request/batch identity guards prevent
an older callback acknowledging newer work. Final source remains frozen.
