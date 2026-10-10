# Cortex trace and training reference

These NumPy tools train optional worker-cap and decision-selection networks. Use
[AI development](../../docs/ai/development.md) for engine changes and the
[quantized model format](../cortex-ml-infer/format.md) for deployment. Training
success does not establish runtime compatibility or improved play.

## Trace boundaries

[AICortexDebug.cpp](../../src/ai/cortex/AICortexDebug.cpp) writes diagnostic CSVs.
Worker rows use the 16 columns in [dataset.py](dataset.py), a swarm identity and
`desired` worker cap. Decision rows use the 48 features populated by
[CortexPolicy::extractDecideFeatures](../../src/ai/cortex/CortexPolicy.cpp), plus
eligibility and chosen-class fields. Loaders require exact column order. Keep
corpora partitioned by source and simulation version; a matching width does not
prove matching feature meanings.

Worker features retain legacy `corn`/`harvestableWheatNearby` names in this corpus
format. The live extractor in [CortexPolicyEconomy.cpp](../../src/ai/cortex/CortexPolicyEconomy.cpp)
reads generalized food-source fields. Verify semantic correspondence before
reusing historical rows.

**Decision corpus compatibility requires review.** The runtime policy now has 19
candidates, including ForwardBase at class 18. Combat classes 15–17 are evaluated
for telemetry but selected by a separate combat pass. The training loader and
exported decision model still have 18 classes (0–17). They neither automatically
filter class-18 labels nor project modern labels to a compatible model. Do not feed
current traces directly into these trainers without checking labels, masks and
intended selection semantics. The binary format alone cannot encode that policy
migration.

## Worker behavior cloning and offline reinforcement learning

[train_bc.py](train_bc.py) learns class `desired - 1` with cross-entropy. Its
masked evaluation permits worker counts from the minimum through the base or
late cap defined in [CortexConstants.h](../../src/ai/cortex/CortexConstants.h).
Wheat-starved rows are excluded by default because the runtime hard clamp bypasses
the model. Standardization is fitted on training data and folded into layer zero
for raw-feature export; inference uses logits and argmax rather than softmax.

[reward.py](reward.py) joins consecutive decisions for the same `gid` within one
team/game file. No join crosses files. It rewards next-state food buffers in the
production band; penalizes stalls, saturated-buffer overstaffing, worker-count
oscillation and hauling into a starved catchment; and attributes sustained labor
shortfall to releasable workers in that swarm. A colony-wide starvation penalty
would apply the same feeding outcome to many swarms and is deliberately omitted.
The constants at the top of the module define weights and a 0.9 decision-cycle
discount. [train_awr.py](train_awr.py) and [train_cql.py](train_cql.py) use those
transitions; [rl_common.py](rl_common.py) shares masking and value estimation.

## Decision reward and selection

[decide_reward.py](decide_reward.py) joins `<prefix>.team<N>.csv` to
`<prefix>.log` and requires its `GLOB2_GAME_END winner_team` result. Terminal reward
is +1 for that team, -1 for another decisive winner, and zero for a draw/timeout.
Missing winner records are errors rather than invented losses.

The shaping potential uses only own-colony military, economy and risk features.
Military and economy terms are normalized by corpus 95th percentiles; risk combines
starvation share and attacked buildings. Their weighted sum is passed through
`tanh`. Nonterminal shaping is `lambda * (gamma * Phi(next) - Phi(current))`;
the terminal outcome is added at the final row. The module computes Monte Carlo
returns with a 0.997 decision-cycle discount. This objective is a training choice,
not a simulation reward or a guarantee of better strategy.

[train_decide_bc.py](train_decide_bc.py) clones eligible hand choices;
[train_decide_awr.py](train_decide_awr.py) uses shaped returns. Export remains
48→64→64→18. At runtime, feasibility gates remain authoritative, selection uses
the economy subset of the mask, and combat runs separately. Empty or out-of-model
masks return no modeled action. Review the current candidate table before changing
class meanings or appending an action.

## Verification

Record corpus source, features, action schema, seed, splits and reward constants.
Check JSON round trips, quantization parity and arithmetic bounds before runtime
use. Then compare complete seat rotations, food sustainability, expansion and
contact under matching simulation versions. Keep corpora, candidate weights and
metrics under ignored `artifacts/`; summarize maintained commands in the
[tool overview](README.md).
