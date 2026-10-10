# Cortex integer model format

`cortex-i16f16-v1` is the binary interchange format used by both optional Cortex
model modes. [quantize.py](quantize.py) writes it, [int_ref.py](int_ref.py) evaluates
reference outputs, and [CortexNet](../../src/ai/cortex/CortexNet.cpp) loads and
executes it in the game. Start with [AI development](../../docs/ai/development.md)
and the [training tools](../cortex-ml/README.md).

## Model types and integration

| Model | Accepted trainer JSON format | Input/output endpoints | Typical training architecture |
| --- | --- | --- | --- |
| Swarm worker cap | `cortex-mlp-f32-v1` | 16 inputs, 20 logits | 16 → 32 → 32 → 20 |
| Economy decision | `cortex-decide-mlp-f32-v1` | 48 inputs, 18 logits | 48 → 64 → 64 → 18 |

Both use the same binary magic, version and arithmetic. There is no separate
model-kind tag in the blob. `load()` validates worker-cap endpoints; `loadDecide()`
validates decision endpoints. Hidden-layer sizes come from the blob rather than
being fixed to the training defaults.

[CortexPolicy](../../src/ai/cortex/CortexPolicy.cpp) selects `ml` with
`GLOB2_CORTEX_POLICY` and loads `GLOB2_CORTEX_NET`, or selects `ml-decide` and loads
`GLOB2_CORTEX_DECISION_NET`. Missing or invalid models leave the hand policy active
with a diagnostic. Model selection happens at construction, not every tick.
[AICortex](../../src/ai/cortex/AICortex.cpp) persists the selected mode and loaded
model snapshot so resumed games do not depend on a changed model file.

## Float input and quantization

The JSON contains `format`, `activation: "relu"`, `arch` and one `layers` entry per
architecture edge. Each entry supplies `W[out][in]` and `b[out]`, representing
`y = W·x + b`. The quantizer accepts the two formats above, checks layer/shape
agreement and converts every weight and bias to a signed 32-bit I16F16 value.

Quantization rounds `v * 65536` halfway away from zero, then clamps to
`[-2^31, 2^31 - 1]`. Trainer normalization must already be folded into the first
layer because inference consumes raw integer features. The quantizer does not
embed feature names, normalization metadata or catalog schema versions in the blob.
It uses the Python standard library; the trainers and corpus checks use NumPy.

## Binary layout

All fields are little-endian. `u32` is an unsigned 32-bit word; `i32` is a signed
two's-complement 32-bit word. Weights and biases are I16F16: integer value divided
by `2^16` gives their represented real value.

| Offset | Type | Field | Value |
| --- | --- | --- | --- |
| 0 | `u32` | magic | `0x434E5831`; disk bytes `31 58 4E 43` |
| 4 | `u32` | version | 1 |
| 8 | `u32` | fractional bits | 16 |
| 12 | `u32` | layer count | `arch_len - 1` |
| 16 | `u32` | architecture length | At least 2 |
| 20 | `u32[arch_len]` | architecture | Input, hidden and output widths |

Each layer follows in architecture order:

| Type | Field | Meaning |
| --- | --- | --- |
| `u32` | input width | `arch[layer]` |
| `u32` | output width | `arch[layer + 1]` |
| `i32[output * input]` | weights | Output-major row order: `W[o * input + i]` |
| `i32[output]` | biases | One bias per output |

The C++ loader rejects incorrect magic/version/fractional bits, inconsistent
architecture counts/endpoints, zero layer dimensions, layer-width disagreement
and truncated payloads. It checks payload bounds before allocating weight arrays.
The current parser accepts trailing bytes; the quantizer writes only the defined
payload. The Python reference is an evaluator for quantizer output, not an
identically strict implementation of every C++ loader check.

## Integer evaluation and range limits

Raw inputs are promoted to I16F16 with `x << 16`. Each product is
`(weight * activation) >> 16` with a signed 64-bit intermediate and arithmetic
right shift. Products are summed, then the bias is added. Hidden layers apply
integer ReLU; the final layer returns logits without softmax. Activations and
accumulators remain signed 64-bit values through the forward pass.

The weight clamp does not prove that every model/input combination fits signed
64-bit products and sums. The C++ evaluator has no runtime overflow guard.
The Python reference uses arbitrary-precision integers and checks the completed
accumulator's range; this does not replace product-range analysis. Validate the
actual exported model against representative and boundary feature values.

Action selection compares full-width logits. `forwardDecide()` separately narrows
logits to signed 32-bit values for the diagnostic/parity interface; those narrowed
values are not the values used to select an action.

## Worker-cap selection

The 16 features, in order, are supply stock/capacity, current worker request,
inside occupancy/capacity, nearest food-source distance, harvestable food sources
nearby, free workers, total free units, total needed workers, worker population,
swarm count, feeding capacity, starving units, food demand and maximum construction
level. The current binding is in
[CortexPolicyEconomy](../../src/ai/cortex/CortexPolicyEconomy.cpp); old corpus column
names such as `corn` and `nearestWheatDist` describe this retained feature order.

1. A known nearby-food count below `CORTEX_SWARM_WHEAT_STARVED_TILES` bypasses the
   model and returns `CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP`.
2. Otherwise evaluate the 20 logits. Valid action `index + 1` must be between
   `CORTEX_SWARM_WORKER_MIN` and the active cap.
3. The late cap applies when maximum construction level reaches
   `CORTEX_SWARM_CAP_LIFT_BUILDLEVEL` and free workers are available.
4. Select the largest eligible logit; equal values choose the lowest index.
   If no class is valid, return `CORTEX_SWARM_WORKER_MIN`.

Current [constants](../../src/ai/cortex/CortexConstants.h) are minimum 1, ordinary
cap 7, late cap 12, cap-lift level 3, starvation threshold 5 and starvation cap 1.
`int_ref.py` mirrors these constants; keep the mirror synchronized with C++.

## Decision selection and catalog projection

[CortexPolicy::extractDecideFeatures](../../src/ai/cortex/CortexPolicy.cpp) defines
the 48-feature order. `scoreDecision()` applies an eligibility bit mask over 18
candidate indices and chooses the highest full-width logit, with ties going to the
lowest eligible index. An empty mask or a mask without a valid candidate returns
`-1`; eligibility remains a policy decision outside the model.

The runtime candidate table has 19 entries, with ForwardBase at index 18. The
model has only 18 outputs, so it cannot select that candidate. Combat candidates
are selected by a separate policy pass; model inference receives the economy mask.
See [trace and training compatibility](../cortex-ml/training.md#trace-boundaries)
before training with current decision traces.

The current [observation/action interfaces](../../src/ai/cortex/CortexTypes.h) are
versions 23/14. They use semantic building roles independently of catalog IDs.
Policy counts can include several capabilities of one building, whereas the fixed
model vector uses the exclusive projection assembled in
[CortexObservation](../../src/ai/cortex/CortexObservation.cpp): one supported role
channel per building and one count per upgradable building. Per-class production
bindings are policy-only and do not change model vector widths. Stock-trained
models can load with other catalogs; their strategic quality is not guaranteed
by the projection or endpoint checks.

## Verification

Run the registered `CortexNetCoverage` cases for loader rejection, independent
integer-output oracles, ties, masking and worker caps. [parity.py](parity.py)
compares a seeded worker-cap model on generated inputs with the standalone C++
runner; [verify_net.py](verify_net.py) checks a supplied worker-cap model on corpus
rows and reports float-versus-quantized agreement. Their compiler include paths
are host-specific and their scratch files currently go under `.tmp/`; inspect
configuration before running them on another host.

Model arithmetic parity on a tested corpus is separate from full simulation
checksum equivalence across supported platforms. Use the repository's
[compatibility verification](../../AGENTS.md) for model/controller changes that
can alter orders.
