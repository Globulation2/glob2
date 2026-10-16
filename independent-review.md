# Independent review: delayed gradient publication

Reviewed feature: PR #1194 against released base `987c953698bb3f0bfd1078184773075ba3313091`, including the append-ordered intrusive populations and completed-world observation integrations. Final source/document review: `f56cbc8f6` (parent `0b392483dd3d3e12ca5d01ba1aa8ab0e30ac2ac1`). Format **168**, SIM **66**, replay floor **168**, supported full-save floor **58**.

## Conclusion

No remaining concrete production blocker was found in this independent review. This conclusion concerns architecture, ownership and the reviewed source; it does not substitute for the parent’s final-revision verification or unexecuted platform coverage. The exact-final-head consumer qualification completed as recorded below; publication of accessible evidence and wider verification remain parent-owned.

## Ownership and invariants

- Map-owned adapters enroll deduplicated requests, validate stable identity/hard epochs, admit FIFO tickets and publish at fixed logical boundaries. Soft dirtiness coalesces a later refresh; worker completion cannot choose the simulation tick.
- Existing GradientPipeline and BuildingGradientPipeline reuse ComputeExecutor. Workers consume immutable captured primitive inputs and own complete private outputs and scratch. No second executor or recursive dependency on live missing fields was introduced.
- Unit/building/team consumers explicitly handle Pending, retain applicable assignments and reservations, and validate current local movement legality. Ground material/area arrays are borrowed during the current owner decision; building and air views expose immutable leases with documented lifetimes.
- AirDestinationSelection and the bounded air kernel own enumeration and propagation. Shared fields retain bounded goal lists; private choices publish only their selected result.
- MapIO and air serialization persist identities, queue order, complete private results and remaining logical advances. Completed-world markers begin at format167 independently of delayed inventories at168. Historical pending outputs remain representable through the explicit test adapter without silently discarding them.
- Intrusive population capture preserves authoritative append order and gid-indexed lookup, including sparse clearing claims. Goal tie ranks remain explicitly row-major. Completed-world publication/capture/submission occurs before orders; live-step requests enter a subsequent boundary.

## Concrete findings resolved

1. Private air clearing choices retained map-sized vector capacity after clear(), outside their reservation charge. Candidate goals/seeds are now worker-local; dense-choice coverage asserts zero retained goal/label/cost capacity.
2. Output accounting now uses fixed ABI-independent Goal16/Choice24 allowances with static assertions, explicit one-goal-per-cell reserve, and matching loader bounds. The 64MiB reservation accounts runtime-owned published/admitted/boundary-retired output generations; it is not a total heap ceiling. Scratch, snapshots, spare buffers and arbitrarily retained external leases have separate lifetimes.
3. Ground supplier destination lookup incorrectly treated ascent’s exact-AT_GOAL result as reachability, rejecting finite pickup-penalty peaks. It now obtains reachability from the completed field and uses ascent for coordinates. A nonzero-penalty regression checks Reachable, valid supplier contact/peak and successful hiring.
4. Consumer material pool policy was absent from captured validity. excludesShared now participates in request/lookup/worker/publication validation, persistence and current checksums; historical loads derive it from the loaded consumer. Superseded policy cannot publish into the same identity slot.
5. Parent continuation testing exposed a text-section collision between numeric pending-job and queued-request paths. The new queuedRequests namespace preserves both inventories independently; binary positions and old-format decoding remain separately gated.
6. Consumer fixtures were corrected to observe real Pending/publication semantics rather than demand synchronous cold results or compare undefined failure outputs. Strong cargo/claim/RNG/clock/reservation and per-tick continuation assertions were retained. Delivery extensions require an actual positive-speed final delivery already in progress, preserve the original512 per-tick audits, and remain bounded.
7. The old167 warm consumer-cache decoding gap now has a dedicated component migration test followed by full current-save continuation. It verifies complete arrays, captured metadata and reconstructed pool policy. This is accurately a historical component decode plus full continuation, not a genuine historical full167 warm save.

Additional coverage inspected includes hard deletion/gid reuse across queued/admitted/completed-unpublished states and binary/text saves; weighted seeds compared against independent wide oracles; admission ordering, worker/serial publication traces and current-local-safety assertions. The faithful legacy writer has an explicit representable-pending mode; its default remains strict.

## Executed consumer evidence

Native macOS ARM64, Apple clang21, fast development build (-O0 -g1), binary source `7bf507b1001197fdcac886f4a3686d03a932b494`. Exact commands and inventory are retained beside JUnit/log evidence:

- `artifacts/delayed-gradients/observation-mac-consumers`: **61 selected,60 passed,1 stale rally checksum failure,0 skipped**. This includes all BuildingProductionCombat, NumbiObservation, FetchHiringScore and SimulationReadPhase cases; warm167 consumer migration; conversion/claim/cargo and high-ID cases; water-only queued/pending/superseded continuation; feeding/healing/training; private flying hiring; and medical air Pending/resumption across participant configurations.
- `artifacts/delayed-gradients/observation-mac-rally-record`: narrow authorized checksum regeneration passed all behavioral assembly/movement/collision/state assertions.
- `artifacts/delayed-gradients/observation-mac-rally-verify`: **1 passed**, ordinary verification without recording or fixture-update flags.

Exact-final-head rerun: `artifacts/delayed-gradients/ready-final-mac-consumers` records **61 passed,0 failures,0 errors,0 skipped**, exit0 on `f56cbc8f624dd35c2066cce1aad51d0d9ada25cf`, with no fixture updates. command.txt, inventory.json, provenance.json (binary SHA256/compiler/platform/source) and actual aggregate junit.xml accompany run.log. The first final attempt encountered filesystem exhaustion after60 passes; its log/provenance/inventory are preserved as interrupted-* and are not represented as a passing run. After parent cleanup, the full61-case rerun passed with2.4GiB still free. Earlier failing diagnostic runs likewise remain available.

## Documentation cleanup

Commit `f56cbc8f6` corrects stale optional-market-pipeline/cancel-on-stock-change wording, clarifies prewarming only an already active pipeline and avoids implying every Game-less map supports building routing. Reviewed architecture prose correctly distinguishes the logical air output reservation from total memory and documents mutable ownership/lease boundaries.

## Limits

No performance work, wall-clock comparison or speedup claim was made. Immediate air A* and hazardous-terrain escape remain owner work outside this field conversion. Explicit editor/reference builders and single-participant execution are documented exceptions. Arbitrary long-lived external air leases remain outside the logical reservation budget.

The strengthened browser test was source-reviewed: one test executes serial/threaded variants, verifies exact non-skipped JUnit inventory and trace lengths/provenance/binary hashes, and compares38 complete per-boundary traces byte for byte. Local Playwright execution was unavailable (package absent); JavaScript syntax checks are not browser verification. This reviewer did not execute Linux, Windows/x86, mobile or browsers. Parent/other agents own the wider engine, legacy, replay/golden and Linux evidence; consult their actual artifacts rather than inferring those results from this review.
