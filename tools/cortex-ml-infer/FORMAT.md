# Cortex I16F16 inference blob — `cortex-i16f16-v1`

Versioned binary layout holding the MLP architecture plus quantized weights and
biases for the Cortex swarm worker-tuning net. Owned end-to-end by the inference
track (quantizer in `quantize.py`, numpy reference in `int_ref.py`, C++ loader in
`CortexNet.{h,cpp}`) so the Python and C++ sides cannot drift.

## Fixed-point representation

All weights and biases are stored as **I16F16**: a signed 32-bit integer holding a
real value scaled by `2^16` (16 fractional bits). This is the project's
deterministic fixed-point format (see `docs/rust/determinism.md`).

- Quantize a float `v` to I16F16: `q = round(v * 65536)`, with round-half-away-
  from-zero (Python `int(floor(v*65536 + 0.5))` for v>=0, symmetric for v<0),
  then clamp to the signed int32 range `[-2^31, 2^31-1]`.
- A fixed-point multiply of two I16F16 values `a`, `b` is
  `(int64(a) * int64(b)) >> 16` using an **arithmetic** right shift (sign-
  preserving). The int64 intermediate prevents overflow; the `>>16` removes one
  of the two `2^16` scale factors.

The forward pass converts each RAW integer input feature `x` to I16F16 by
`x << 16` so the entire matmul is a uniform I16F16 dot product. Equivalently the
layer-0 product reduces to `W_q * x`, but the code uses the uniform `(a*b)>>16`
path for every layer so Python and C++ share one arithmetic.

## Endianness & types

Little-endian throughout. Field types:

- `u32` — unsigned 32-bit little-endian.
- `i32` — signed 32-bit little-endian (two's complement). All I16F16 values use
  this.

## Byte layout

```
offset  type      field            value / meaning
------  --------  ---------------  ------------------------------------------------
0       u32       magic            0x434E5831  ('C','N','X','1' little-endian -> bytes 31 58 4E 43)
4       u32       version          1
8       u32       fixed_frac_bits  16   (sanity: the I16F16 fractional bit count)
12      u32       num_layers       L    (== len(arch) - 1; here 3)
16      u32       arch_len         len(arch)  (here 4: 16,32,32,20)
20      u32[arch_len]  arch        layer sizes: arch[0]=in, ..., arch[L]=out
...     (then, for each layer l in 0..L-1, in order:)
        u32       in_dim           arch[l]
        u32       out_dim          arch[l+1]
        i32[out_dim*in_dim]  W     row-major, out-major: W[o*in_dim + i] = W[o][i] (I16F16)
        i32[out_dim]         b     bias per output unit (I16F16)
```

`W[o][i]` multiplies input `i` into output `o` (`y = W·x + b`), matching the f32
JSON layout in `ML_CONTRACT.md`.

The `magic` is written as the u32 little-endian value `0x434E5831`; on disk the
first four bytes are `0x31 0x58 0x4E 0x43`.

## Activation

ReLU between layers (integer `max(0, v)` on the I16F16 accumulator), applied after
every layer EXCEPT the final logit layer. The architecture is fixed as
`16 -> Dense(32) -> ReLU -> Dense(32) -> ReLU -> Dense(20)`; the blob stores the
sizes so a loader can validate them.

## Inference rule (applied to the 20 logits)

Implemented identically in `int_ref.py` and `CortexNet.cpp`. Constants are read
from `glob2/src/ai/cortex/CortexConstants.h` (mirrored into the code, not the
blob):

1. **Wheat-starved hard clamp (bypass the net):** if
   `harvestableWheatNearby >= 0 && harvestableWheatNearby < CORTEX_SWARM_WHEAT_STARVED_TILES`,
   return `CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP` (1) without running the net.
2. Otherwise run the net to produce 20 I16F16 logits.
3. **Mask:** valid action `k+1` lies in `[CORTEX_SWARM_WORKER_MIN .. swarmWorkerCap]`,
   where `swarmWorkerCap = (maxBuildLevel >= CORTEX_SWARM_CAP_LIFT_BUILDLEVEL &&
   freeWorkers > 0) ? CORTEX_SWARM_WORKER_CAP_LATE : CORTEX_SWARM_WORKER_CAP`.
   Classes outside this range are excluded from the argmax.
4. **argmax** over the unmasked logits; ties resolve to the **lowest** class index.
   Returned action = `argmax_index + 1`.

If the mask leaves no valid class (degenerate: `swarmWorkerCap < CORTEX_SWARM_WORKER_MIN`),
the rule falls back to `CORTEX_SWARM_WORKER_MIN`.
