# Change or add an AI

AI decisions become ordinary engine orders. Extend an existing strategy when its
execution model fits; introduce a new identity only when it represents a distinct
controller that needs independent selection and persistence.

## Understand the decision boundary

`AI` constructs, loads and saves implementations and binds a controller-private
random stream. `AIImplementation` defines serialization, order generation,
observation requirements and optional execution feedback. Older live-world
entry points coexist with immutable observation execution; worker execution is
explicitly opt-in through `supportsObservation()`.

Use immutable `AIEngine::DecisionContext` inputs and controller-private query
caches for migrated decisions. Live `Player`/`Game` access belongs to simulation-owner
construction and load. Do not move a live-world controller to a worker lane merely
by changing its support flag. Keep hidden-enemy reads consistent with the
controller's intended fog/visibility policy.

Sources: [wrapper](../../src/ai/AI.cpp), [implementation interface](../../src/ai/AIImplementation.h),
[observation queries](../../src/ai/observation/AIQueries.cpp) and
[engine decision types](../../src/ai/engine/AIDecision.h).

## Implement behavior

Return a real order or `NullOrder`, never a null pointer. Use the bound random
stream, never `rand()` or wall-clock choices. Decision results and resumed state
must reproduce from the same setup and orders. Treat admission receipts as
acceptance, not completion of construction. Use lifetime-aware references for
pending buildings so recycled slots cannot receive stale work.

Read resolved building capabilities, resource-to-material supplies and game rules.
A catalog need not provide every stock service. Handle an absent capability and
bounded path/placement failure without spinning or using unbounded retries.
Respect rules that disable training or building upgrades and release obsolete
worker assignments where appropriate.

For Maxima, follow its [strategy pages](maxima/README.md) and
[configuration schema](maxima/configuration.md). For Cortex, keep observation and
policy fields synchronized; the [training tools](../../tools/cortex-ml/README.md)
describe exported policy formats. Optional models still require deterministic
inference and compatibility verification.

## Register a new native controller

Add a stable `AI::ImplementationID` without reusing or renumbering existing IDs.
Add construction and load dispatch in `AI.cpp`, implement version-aware load/save,
and include the source and harnesses in the existing build definitions. Update
selection names, translations and any catalog/headless mappings that enumerate
controllers. Follow an existing controller's registration through its UI and
command paths rather than assuming the enum is the only list.

Add telemetry identity/schema only for meaningful measurements; use the
[telemetry contract](telemetry.md) for field retention and freshness. Tests and
examples must use the same public order/capability interfaces as the controller.

## Verify and review

- Run focused strategy and affected engine harnesses, including order/rule guards.
- Test identical setup, seed and orders for deterministic continuation; compare
  per-tick checksums across affected supported platforms.
- Save during pending work and resume, including recycled building identities,
  rejected orders, missing capabilities and disabled-upgrade setups.
- Exercise existing-save loading and current replay/network acceptance boundaries.
- Use populated games with fixed seeds and complete seat rotations; inspect food,
  population, construction and eventual contact before interpreting wins.
- Measure controller time with several simultaneous AIs. A smaller CPU sample
  does not establish better strategy or player enjoyment.

Any change that can alter decisions needs `SIM_REVISION` and the golden match
record updated under [repository compatibility rules](../../AGENTS.md).
Keep previous-version cohorts separate. Record tested source/build identity,
settings, commands, omissions and accessible saves/replays in review evidence;
keep transient analysis in `artifacts/` or `docs/.work/`.

Related: [AI documentation](README.md).
