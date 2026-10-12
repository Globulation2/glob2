# Independent protocol/transport review

Reviewed Protocol, HttpServer, Tools and protocol/HTTP tests against the official MCP 2026-07-28 and 2025-11-25 specifications.

## Findings and fixes

- P2: Fractional JSON-RPC IDs were accepted and explicitly asserted in tests. MCP requires string or integer IDs. Reject them, including null IDs, and change the regression assertion.
- P2: Malformed legacy clientInfo metadata could throw out of Protocol, causing HTTP fallback errors to omit the known request ID. Invalid envelope/params fields also discarded a readable ID. Preserve a valid ID before envelope validation; validate metadata shapes explicitly for both eras; add malformed metadata and parameters tests.
- P2: Absent and explicitly empty Origin were treated identically; duplicate HTTP metadata could be read using only its first value. Reject present empty or duplicate Origin, duplicate Host and duplicate MCP metadata headers. HTTP tests are owned by the independent quality reviewer.
- P2: Extension handlers could return null Request, producing a successful response without content. Reject that programming error at the registry boundary.
- P3: Tool output schemas provided no discoverable fields or defaults. Supply typed viewport/frame/controller/completion/error fields, token/lease fields for acquisition, and documented capture/click/drag defaults.
- P3: Compressed one-line protocol and transport code obscured error and lifetime invariants. Separate parsing/client identity/base64 helpers; use conventional branching; apply repository clang-format configuration to owned files with isolated clang-format 18.1.8 tooling.
- Strengthen encoded MCP-name validation by checking canonical Base64 padding and alphabet through decode/re-encode equality.
- Expose the actual bound HTTP port so transport tests can use port zero without collisions; UI settings continue to require normal ports.

## Validation and remaining limits

Parent agent integrates affected builds and regressions. g++ C++20 syntax verification passed for Protocol.cpp, Tools.cpp and HttpServer.cpp using bundled nlohmann headers and the pinned SDL prefix.

PNG encoding runs synchronously on the HTTP io_context thread. Async stop()/stopped() retirement now keeps the UI responsive during disabling/restarting; UIService reaps only exited workers and defers replacement binding until retired listeners release their ports. Final application teardown joins workers before protocol and SDL destruction. Completion rechecks cancellation, stop and the operation deadline after compression. Network timers still cannot run during that compression; a separate encoding worker could improve concurrent HTTP responsiveness, but synchronous encoding no longer blocks normal UI reconfiguration.

The registry intentionally supports a limited JSON-schema vocabulary, documented in the development guide, rather than a full schema engine. New tools must keep input schemas within that vocabulary and provide domain validation. A registration-time supported-schema audit would further protect extension authors from accidentally advertising unenforced constraints.

Official references:
- https://modelcontextprotocol.io/specification/2026-07-28/basic
- https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http
- https://modelcontextprotocol.io/specification/2025-11-25/basic/transports

## Second-round cross-review of UI/capture integration

Identified cleanup releases appended before ordinary screen dispatch (susceptible to dropped-tail transitions), dropped cleanup-only batches when no active request remains, and capture availability remaining stale after an input opens a native dialog. The independent UI reviewer fixed all three and added regressions. Second-pass inspection confirms releases remain queued until tracked extra-input admission; partial cleanup-only batches retain undelivered releases; afterDispatch refreshes native-dialog/window availability before starting capture. Remote cleanup events are excluded from human ownership observations. ScreenStack keeps one updateExecution per frame; additional input is admitted after human actions and geometry changes. Shared capture pixel ownership and listener/protocol retirement lifetime remain sound.

Independent UI cross-review also identified substring/case-sensitive content negotiation. Fixed HTTP media-type token parsing, case-insensitive types, Accept quality-zero exclusion and repeated Accept aggregation. The quality reviewer owns associated HTTP regressions. Final syntax checking passed after these changes.

## Final buffered-screen API review

Resolved the final buffered-consumer defects through actual dispatch receipts:

- GameSessionScreen tracks exact additional-input ranges through its queue and transfers receipts into Engine's sessionInput queue. Engine completes them only after clientStep/GameGUI::step consumes the range, including delayed turn cadence and first threaded scene readiness.
- Cancellation removes only tracked remote ranges and preserves later human events and retained UTF-8 payloads. Queue offsets are adjusted after removal; teardown, suspend and viewport changes retire affected receipts explicitly.
- ScreenStack retains receipts across frames, maps buffered-consumer indices back through application-global intercepts, and serializes pending batches. Its sole updateExecution per frame remains unchanged.
- The hasPendingExecutionInput capability guards previously queued human geometry/actions, rather than only the current frame's event list. Cleanup has separate admission and is not starved by ordinary human motion.
- MapEditor reports the actual consumed prefix, including persistence early-return and load/quit loop interruption. It no longer acknowledges mere event enqueueing as dispatch success.
- Hidden hosts poll old receipts and drain a separately tracked cleanup batch. No unconditional acknowledgement clears a pending buffered request. Application member order keeps UIService alive through ScreenStack callback retirement.

Added EngineSession regression `buffered input receipts wait for GUI dispatch and cancel exact ranges [display]`: a real initialized engine keeps the receipt pending for two deferred GUI ticks, completes it only when GameGUI sees the motion, retains remote UTF-8 after source-string mutation, cancels its exact range, and preserves appended human UTF-8 through eventual dispatch. Parent runs final builds and runtime verification.

Independent quality review of the new Engine range/receipt implementation passed. Owned production source edits are frozen for final validation.

The final timing observation was resolved by the UI reviewer: pending Down timers start at actual consumption, persistent hold timestamps refresh on completed Down receipts, and only the first drag Down receipt anchors dragStart. Independent reinspection passed; simulated delayed-receipt regressions cover the behavior without long sleeps. Request deadlines and lease expiry still bound pending work.

## Last narrow cancellation review

Independent inspection passed for final UIService corrections: an expired temporary drag hold cancels its remaining queued motion range before consumer dispatch; delayed coordinate batches compare their accepted viewport against current geometry on the main thread. Predicates hold a weak reference to the exact Request and annotate failure reasons only while that request remains active, so old cancellation work cannot complete or alter newer requests. UIService outlives ScreenStack and Engine queues during callback retirement. Tests cover expiry while motion is buffered and resize between generation and consumption. No further production source edits were needed.

Final availability additions also passed independent inspection: the pre-dispatch pump cancels pending non-release input when the UI becomes unavailable, and buffered-consumer cancellation rechecks native-dialog/window availability immediately before dispatch. Only explicit input/control releases remain eligible while unavailable. Failure reasons are applied to the exact active Request; cancelled older predicates return without altering newer work. Narrow review complete and source remains frozen.

## SDL name lookup threading correction

Parent identified SDL's documented main-thread requirement for scancode-name lookup. Removed SDL_GetScancodeFromName from network-thread validation. registerUITools now snapshots SDL names during main-thread startup into a shared immutable catalog; validators perform ASCII case folding and catalog lookup only. The snapshot preserves default names, first-match duplicate behavior and the eight modifier scancodes. Registration's thread contract is documented in Tools.h. Added async worker validation with lowercase/mixed-case aliases plus invalid name/modifier/NUL regressions. Syntax checking passed; independent UI reviewer inspection passed. Main-thread dispatch continues using SDL lookup normally.

## HTTP error delivery after consumer cancellation

Parent found a cross-layer integration defect: UI execution failures now mark Request::cancelled to retire queued input, while HTTP completion mistakenly treated that flag as transport disconnect and dropped the completed tool error. Ready-result publication now depends on Session.closed and listener stopping only. Completed UI failure results remain JSON HTTP200 with isError and the original request ID; EOF and listener stop retain their existing cancellation behavior. Added a real HTTP regression whose context sets cancelled=true and completes an execution failure. Independent UI reviewer inspection passed with no disconnect/stop regression. Source frozen for final build and runtime tests.

Final dispatch metadata review passed: a separate dispatchAcknowledged bit records actual final consumer acknowledgement, so staging a release for later cleanup cannot falsely report dispatched=true. A later cleanup/capture failure preserves an already acknowledged final input batch. State resets on new requests, finish and capture completion; exact batch/request snapshot callbacks continue protecting newer work. Regressions cover staged-release cancellation and acknowledged Down followed by interrupted capture cleanup. No further source edits required.
