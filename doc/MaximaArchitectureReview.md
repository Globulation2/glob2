# Maxima architecture review — September 5, 2026

Maxima has useful isolated algorithm modules, but it does not yet meet the goal of highly modular subsystems around a small core. The next architectural step is to move subsystem state, lifecycle, and policy ownership out of `Maxima`, alongside the algorithms already extracted.

This review covers the current local working tree, including untracked Maxima sources. It is an architecture review, not a claim that the findings below are demonstrated gameplay failures. No implementation changes were made.

The Maxima C++ headers and sources contain 23,620 physical lines, including comments and whitespace. `AIMaxima.cpp` has 9,406 lines, `AIMaxima.h` has 911, and `AIMaximaFarmingPolicy.cpp` has 2,804. Those three files still implement one class and account for about 56% of Maxima source. Size is supporting evidence; shared ownership and unrestricted access are the underlying problems.

**What is already working well**

| Area | Existing boundary | Assessment |
| --- | --- | --- |
| Placement | `WorldState`, intents, limits, action lifecycle, `Planner` | Strong foundation: explicit inputs, owned reservations and actions, incremental selection, and revalidation before issuance. Its stream serialization remains a support-library dependency. |
| Reconnaissance | Sightings, opponent memory, reports, objectives | Useful independent observation model. Engine observation and mission execution still live in the core. |
| Tactics | Raid observations, candidate scoring, mission data, target quarantine | Useful independent policy primitives. Target selection and the flag controller remain in the core. |
| Defense | Movement masks, topology policy, candidate/result types | Clear deterministic algorithm boundary. Reactive defense and runtime reconciliation remain in the core. |
| Farming | Fertility cache, circulation and porosity algorithms | Good independent primitives. The much larger farming policy is still part of `Maxima`. |
| Configuration | Grouped strategy values, resolver, parameter schema and provenance | Keep this separation. `AIMaximaStrategy` primarily means configuration; it does not own strategic decision-making. |
| Runtime | Order queues, building tracking, gradients, management conditions | Useful separation from the shared Echo implementation. Its public engine access is too broad to serve as a restrictive policy interface. |

The existing deterministic scheduling, explicit placement lifecycle, and standalone tests are assets to preserve.

**1. High priority: the core still owns the major subsystems**

The nested [StrategyDirector](/Users/bradley/glob2/src/AIMaxima.h:56) owns two flags. Its [evaluate method](/Users/bradley/glob2/src/AIMaxima.cpp:792) calls `owner.evaluate_strategy(echo)`. Strategic state, scoring, arbitration, environment analysis, opponent assessments, and plan construction all remain on `Maxima`.

Combat used to show the same pattern most sharply: tactical authorization ran
roughly 660 lines and attack control roughly 480, with muster and withdrawal
transitions, target selection, engine queries, order issuance and diagnostics
interleaved. The relentless offense replaced both with `plan_offense` (about
250 lines, which picks the target) and `control_offense` (about 195, which
keeps one flag on it). `AIMaximaTactics.cpp` still does not own that lifecycle,
but there is far less of it to own.

Every member method can access every subsystem's state. Moving more `Maxima::` methods into separate files would improve navigation without establishing a stronger boundary.

Extract concrete subsystem objects that own their state and expose narrow operations. A real director should accept observations and subsystem reports, update its own strategic state, and return a plan. It should not take `Maxima&`. Combat should own mission state and flag bookkeeping. Keep `Maxima` responsible for composition, scheduling, event routing, and the `AIImplementation` entrypoints.

**2. High priority: strategic authority is only partially expressed by the interfaces**

The [DirectorPlan contract](/Users/bradley/glob2/src/AIMaxima.h:145) says executors consume an immutable output and perform tactical validation, geometry, pathfinding, and order issuance. In practice, the plan is a mutable member containing 175 field declarations, mixing strategic decisions, copied configuration, and tactical observations.

There are also executor paths outside the stated boundary:

- [Gate-clearing eligibility](/Users/bradley/glob2/src/AIMaximaFarmingPolicy.cpp:732) combines the plan with direct strategy, economic-model, and strategic-snapshot reads to decide whether costly clearing is affordable.
- [Tower staffing](/Users/bradley/glob2/src/AIMaxima.cpp:7007) re-evaluates the explorer emergency and reads staffing configuration while issuing assignments.
- [Development limits](/Users/bradley/glob2/src/AIMaxima.cpp:6467) obtain repair authorization directly from configuration.
- [Firebreak selection](/Users/bradley/glob2/src/AIMaximaFarmingPolicy.cpp:1386) reads configuration alongside plan fields.

Reading immutable configuration is not inherently wrong. The problem is that strategic authorization, fixed execution rules, and permitted live safety checks are not distinct contracts. An isolated executor cannot currently run from its advertised plan alone, and reviewing one strategic decision requires following helper calls into shared state.

Split the aggregate into typed subplans such as `EconomyPlan`, `DevelopmentPlan`, `CombatPlan`, and `LandUsePlan`. Pass each executor a const subplan and, where appropriate, a narrow immutable rules object. Explicitly distinguish strategic authorization from live validation that can reject an action. Build the complete next plan before publishing it. Avoid making every executor depend on the entire configuration or the entire director plan.

**3. High priority: farming, access, placement, and gate defense share implicit state contracts**

`AIMaximaFarmingPolicy.cpp` has no independent policy object. It implements `Maxima` methods and accesses placement internals, engine objects, director invalidation, and shared map masks.

The coupling works in both directions:

- [Maintenance clearing](/Users/bradley/glob2/src/AIMaximaFarmingPolicy.cpp:1291) reads planner reservations; related helpers inspect campus and action records.
- [Placement-world collection](/Users/bradley/glob2/src/AIMaxima.cpp:6250) imports farming protection and gate/escape constraints.
- [Gate defense](/Users/bradley/glob2/src/AIMaximaFarmingPolicy.cpp:2487) computes tower demand and placement quality inside farming policy; strategic bidding consumes that demand.
- [Farming execution](/Users/bradley/glob2/src/AIMaximaFarmingPolicy.cpp:2739) relies on a specific sequence of porosity, access repair, defense refresh, and protection reconciliation. The comment explicitly explains why reversing the order can forbid an exit again.

These interactions are legitimate, but their current contract is shared fields plus call order. That makes local changes difficult to reason about.

Create a land-use subsystem with a clear owner for protection, gates, access routes, and clearing obligations. Feed it a read-only placement-reservation view; publish a versioned spatial-constraints view for placement and a gate-coverage request for defense. Keep the precedence between building access, farming protection, and clearing explicit in one reconciliation step. Preserve which map cells Maxima owns and is allowed to remove. Do not replace these small, direct data exchanges with a generic event bus.

**4. Medium priority: initialization, persistence, and obsolete behavior enlarge the active state surface**

The [constructor](/Users/bradley/glob2/src/AIMaxima.cpp:5075), [legacy loader](/Users/bradley/glob2/src/AIMaxima.cpp:5380), and [current loader](/Users/bradley/glob2/src/AIMaxima.cpp:5595) manually initialize or reset many of the same subsystem fields. This makes introducing a new state field a coordinated edit across multiple large functions.

The live class also retains [legacy plans](/Users/bradley/glob2/src/AIMaxima.h:566), [old phases and construction queues](/Users/bradley/glob2/src/AIMaxima.h:632), and duplicate initialization flags for compatibility. The private `control_legacy_attacks` implementation has no caller in the searched C++ sources; its calls to `attack_building` and `choose_building_to_attack` form an otherwise unused chain. Python structural tests still use some of these signatures as text delimiters.

Give each subsystem a state type with its own default construction and transient-state reset. Isolate version-specific decoding in a persistence adapter, consuming obsolete fields into temporary compatibility data where possible. Preserve the existing wire format, enum values, section names, and load-time rebuild semantics. Separately remove the unused combat chain after checking references and updating the text-based tests. Do not combine save-format changes with a behavior-preserving extraction.

**5. Medium priority: runtime access and diagnostic formatting cut across policy boundaries**

[Context exposes a public Player pointer](/Users/bradley/glob2/src/AIMaximaRuntime.h:622), allowing callers to traverse into the full game, team, and map. Core policy methods also use `globalContainer`. Consequently, the runtime currently provides convenient engine services without constraining what a policy can observe or mutate.

Introduce small observation collectors and an order adapter as modules are extracted. Pure policy should consume explicit data; engine-aware collectors should own visibility checks and engine-to-domain conversion. Existing `WorldState`, `ModeInput`, and sighting types demonstrate this approach already. Use concrete adapters unless multiple implementations actually require virtual interfaces.

[HUD formatting](/Users/bradley/glob2/src/AIMaxima.cpp:811) and [director telemetry](/Users/bradley/glob2/src/AIMaxima.cpp:4688) together occupy hundreds of core lines. Move formatting into diagnostics code that consumes read-only reports, while retaining decision reasons at the point where policies produce them. Preserve telemetry names used by analysis tools.

**6. Medium priority: some tests preserve implementation layout rather than enforce modularity**

The [director authority tests](/Users/bradley/glob2/test/MaximaDirectorAuthorityTest.py:27) look for class names, source ordering, and forbidden strings in selected text ranges. They pass today despite the direct strategy reads listed above: the ranges omit relevant helpers and executor paths. They also expect members such as `DirectorPlan budget;`, which will change in a useful extraction.

Six Maxima C++ test files use `#define private public`. These tests contain valuable behavioral regressions, but their fixtures depend on the layout of the full AI and frequently link engine objects to exercise a small policy concern.

As each subsystem moves, migrate its existing scenarios to the new public boundary. Keep focused engine integration tests for order translation, scheduling, visibility, and persistence. Add dependency checks that prevent pure policy modules from including `Game.h`, `Map.h`, `GlobalContainer.h`, or the core AI header. Prefer behavioral assertions over checks for exact helper names or file placement.

**Recommended organization and sequence**

An eventual source layout could look like this; create directories as ownership becomes clear, rather than doing a large file move first:

```text
src/ai/maxima/
  Maxima.h/.cpp                 composition and AI entrypoints
  scheduling/                   logical cadence, pending jobs, event routing
  strategy/                     director, assessments, arbitration, typed plans
  economy/                      staffing, production, retirement
  development/                  placement planner and execution adapter
  land/                         farming, barriers, access, clearing ownership
  combat/                       tactical authorization and mission lifecycle
  recon/                        observations, memory, scout missions
  defense/                      topology, reactive defense, guard reconciliation
  runtime/                      engine observations, orders, tracking, gradients
  config/                       strategy values, schema, resolution
  persistence/                  versioned decoding and state migration
  diagnostics/                  HUD and telemetry formatting
```

Use compile-time composition and explicit typed inputs/outputs. This AI does not need a plugin framework, dependency-injection container, or abstract interface for every helper.

1. Establish behavioral baselines and shared test fixtures. Record deterministic order traces for representative fixed scenarios where practical.
2. Extract a small, complete economy subsystem first: inn/swarm staffing and retirement state, with an explicit plan and observations. This exercises the ownership pattern around existing standalone staffing primitives.
3. Extract land-use state and policy, introducing explicit placement and defense views. This removes the separate-file/same-class coupling at its largest concentration.
4. Extract combat and reconnaissance controllers, including their events, flag records, and state transitions. Replace parallel per-flag maps with owned records where their lifecycles match.
5. Move strategic state and decision-making into the director, passing subsystem reports and returning typed plans. Reduce the remaining core to orchestration.
6. Move diagnostic formatting and versioned persistence behind their own boundaries, then finish directory and naming cleanup. Update SCons, Windows projects, and test/tool source paths together.

For every extraction, preserve deterministic iteration and tie-breaking, logical AI ticks, pending-order draining, deferred map work, fog-of-war semantics, and map-area ownership. In particular, [runtime getOrder](/Users/bradley/glob2/src/AIMaximaRuntime.cpp:1129) drains queued orders before advancing the AI; replacing its cadence with the game frame counter would change behavior.

Completion should be judged by ownership: a subsystem owns its state and reset behavior, can be tested through its public inputs and outputs, cannot reach into `Maxima`, and communicates with peers through explicit views or results. A shorter core is a consequence of those boundaries, not the primary acceptance test.

**Validation**

- Director-authority structural suite: 15 tests passed.
- Runtime-decoupling structural suite: 3 tests passed. Its symbol check uses existing tournament build objects.
- Fresh standalone C++ compilation and execution: defense, farming, reconnaissance, tactics, and staffing passed.
- Native runner (`python3 test/run_maxima_implementation_regressions.py --build-dir build-tournament`): combat integration passed, then general implementation integration aborted at [MaximaImplementationIntegrationTest.cpp:201](/Users/bradley/glob2/test/MaximaImplementationIntegrationTest.cpp:201), on `assert(ai.choose_tactical_rally(c,30,64,rallyX,rallyY))`. Subsequent suites in that runner were not executed. The runner freshly compiles Maxima, the AI factory, and building lifetime tracking, and links other engine objects from the existing tournament build. The failure's cause has not been diagnosed; it must be resolved or characterized before treating this as a green refactor baseline.
