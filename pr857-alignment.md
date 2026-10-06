# Alignment with draft PR857

Reviewed PR857 head bf782ac6d275553f0140368fccc961200e5a8fbf against gradient PR789 head 772d0c0566785d6476b94270cf68bce244d0b281. This is an integration contract for the eventual combined change, not a claim that the WIP AI migration has been merged or verified. No simulation code changed during this review.

## Snapshot ownership

Use SimulationSnapshot::Store and Handle::project from PR857. BuildingGradientCapture is the adapter, not another engine snapshot store. Gradient workers should lease Terrain, Resources, Occupancy and Areas (plus Catalogs only where needed); retain destination/supplier scalar records separately. Read these projected contiguous arrays directly, replacing the full Cell repack. Capture the union at the existing completed-world boundary, before worker execution; do not move capture into AI lanes or retain Entities/Teams/Visibility merely to reach map data. Preserve resource PR851 reservation and preparation behavior.

PR857's ResourceFieldKey is tuple(team, resource, swim, bool market). That is insufficient to identify our consumer-specific published resource fields: resourceSupplyModes, building consumer identity/lifetime and supplier eligibility/penalties affect parents. Extend the resource component's keys/leases for those parents, or keep explicit immutable parent/supplier-goal leases in the gradient adapter. Never replace held published parents with a fresh AI-local resource query. Preserve parent versions for auditing and coherent bundle publication.

## Executor ownership

PR857's AIEngine::Executor owns 32 FIFO controller lanes. OrderScheduler owns that executor by value; GradientPipeline still owns a separate resource pool in its current draft. Therefore the published head does not yet expose a shared generic engine pool to gradient jobs.

Extract the low-level persistent worker backend and let both schedulers borrow it. Keep controller FIFO/non-overlap as an AI scheduling policy. Give independent gradient jobs an unkeyed submission path; do not squeeze building identities into controller lanes or serialize them all on one reserved lane. Retain reusable per-worker GradientWorkspace scratch. Configuring one consumer must not reset another consumer's jobs or worker budget. Saving/cancellation may finish private work but must not publish either stream early.

AI X=0..8 and building D=2/4/8 remain distinct simulation rules. Publication order and due waits remain owned by their respective schedulers. Preserve zero-worker and thread-failure determinism and local configuration behavior. Benchmark contention/priority/deadline waits after the shared backend is installed.

## Building access snapshot: actual merge hazard

PR857 WorldRecords.h has BuildingView::locked as array<bool,6>. WorldCapture.cpp copies BUILDING_ACCESS_COUNT elements from b->locked into it. PR789 expands BUILDING_ACCESS_COUNT to 21. These source changes auto-merge, but the combined copy would overflow the six-element destination.

Resolve semantically before running the combined binary. If retaining the six legacy AI feasibility views, fill them through routeAccess(route, legacySwim ? SWIM_CLASS_EVEN : 0), rather than copying the new raw storage prefix. Alternatively expand the snapshot layout and update every controller/query's route/swim accessor together. Distinguish experiment-off legacy mapping from experiment-on class mapping. Add regression cases with all three routes and custom/swimming classes under both configurations, including immutable captured feasibility after live invalidation.

## Persistence and protocol

Both drafts independently claim file format140, replay floor140, network59 and SIM_REVISION23, but GameHeader/save scheduling layouts differ. Equal version numbers do not make those files mutually readable. The second landing change needs a fresh gated format/protocol/sim revision, independent AI/building delay bytes and extensions, and regenerated fixtures. Decide how to treat already-created incompatible draft140 saves explicitly; do not silently claim support for both layouts.

Regenerate protocol manifests/schema fixtures after combining both rules; do not resolve generated conflicts by choosing one branch. Retain older supported released saves (floor58), replay execution-tick behavior, both pending-output continuations, remaining deadlines and later building dirty state.

## Rehearsal and verification

The non-mutating git merge-tree rehearsal found 16 conflicted paths: browser replay fixture; headless/performance docs; protocol manifests and fixture generator; Castor Control; file gates and Headless options; experiment/header tests; rule overrides; Map.cpp; GradientPipeline; MatchSetup; golden multiplayer record; vendored protocol manifest. Full output is retained locally in pr857-merge-rehearsal.txt. Successful automatic textual merges are not proof of semantic compatibility, notably the access buffer and format collisions above.

After integration: AI delay0/1/4/8 × building pipeline off/on and D2/4/8, worker0/1/2/4/8, slow workers/factory failure, simultaneous AI/building deadlines, team death/revival and building reuse, consumer-specific parents/markets, every save phase with both kinds pending, audit-on/off equality, complete macOS/Linux tick traces, replay/network rejection boundaries. Timing remains separate: rerun paired CPU/elapsed/capture/memory/deadline comparisons; do not combine historical PR789 numbers with PR857 preliminary numbers.
