# Measured generator defaults

Generated from the compiled catalog and fixed seed results. Settings below are a documentation snapshot; change the C++ definitions, then regenerate this file.

## Coverage and reliability

All cohorts use the same 1,000 seed attempts per mode (20001–21000); failed attempts are not replaced. Percentages are means of successful maps. Both prior-setting cohorts include the legacy fixes.

| Generator | Grass: lobby → tuned | Free tiles: lobby → tuned | Failures: lobby → tuned | All-team start proxy: lobby → tuned |
|---|---:|---:|---:|---:|
| Swamp | 41.5% → 56.5% | 25.3% → 44.6% | 0 → 0 | 851 → 952 |
| River | 20.2% → 55.9% | 9.8% → 40.7% | 4 → 18 | 177 → 800 |
| Islands | 19.9% → 49.0% | 10.0% → 33.6% | 0 → 0 | 37 → 985 |
| Crater lakes | 18.1% → 65.4% | 9.2% → 57.1% | 0 → 0 | 0 → 923 |
| Concrete islands | 57.7% → 65.6% | 33.0% → 37.6% | 18 → 8 | 978 → 981 |
| Isles | 32.4% → 42.2% | 17.9% → 23.9% | 0 → 0 | 998 → 1000 |
| Shattered Coast | 21.0% → 41.7% | 17.6% → 36.4% | 0 → 14 | 281 → 823 |
| Rugged Archipelago | 25.2% → 39.8% | 18.7% → 29.2% | 0 → 0 | 1000 → 1000 |

## Previous editor comparison

The legacy editor normally applies zero sand/desert weights on its first visible slider timer tick. PR #237’s new lobby used constructor weights of 50 for both. The two previous-setting cohorts are intentionally distinct.

| Generator | Editor free tiles → tuned | Editor failures → tuned |
|---|---:|---:|
| Swamp | 25.3% → 44.6% | 0 → 0 |
| River | 23.1% → 40.7% | 145 → 18 |
| Islands | 22.9% → 33.6% | 0 → 0 |
| Crater lakes | 22.9% → 57.1% | 0 → 0 |
| Concrete islands | 33.0% → 37.6% | 18 → 8 |
| Isles | 17.9% → 23.9% | 0 → 0 |
| Shattered Coast | 30.2% → 36.4% | 2 → 14 |
| Rugged Archipelago | 18.7% → 29.2% | 0 → 0 |

## Shared controls

| Control | Range | Step | Default |
|---|---|---:|---:|
| Width | 64–512 | ×2 | 128 |
| Height | 64–512 | ×2 | 128 |
| Colonies | 1–12 | 1 | 4 |
| Starting workers | 1–8 | 1 | 4 |

Terrain weights are relative weights, not percentages of the final map. Lake size and bridge/channel width are algorithm inputs, not exact final tile dimensions.

## Swamp

| Control | Range | Step | Default |
|---|---|---:|---:|
| Water weight | 0–100 | 1 | 35 |
| Grass weight | 0–100 | 1 | 60 |
| Smoothing | 1–8 | 1 | 6 |
| Fruit | 0–64 | 1 | 4 |
| Repeat landscape | 1–32 | ×2 | 1 |

## River

| Control | Range | Step | Default |
|---|---|---:|---:|
| Water weight | 0–100 | 1 | 45 |
| Sand weight | 0–100 | 1 | 3 |
| Grass weight | 0–100 | 1 | 75 |
| Desert weight | 0–100 | 1 | 0 |
| Smoothing | 1–8 | 1 | 4 |
| River width | 20–65 | 5 | 35 |
| Fruit | 0–64 | 1 | 4 |
| Repeat landscape | 1–32 | ×2 | 1 |

## Islands

| Control | Range | Step | Default |
|---|---|---:|---:|
| Water weight | 0–100 | 1 | 55 |
| Sand weight | 0–100 | 1 | 3 |
| Grass weight | 0–100 | 1 | 75 |
| Desert weight | 0–100 | 1 | 0 |
| Smoothing | 1–8 | 1 | 4 |
| Extra islands | 0–8 | 1 | 0 |
| Fruit | 0–64 | 1 | 4 |
| Repeat landscape | 1–32 | ×2 | 1 |

## Crater lakes

| Control | Range | Step | Default |
|---|---|---:|---:|
| Water weight | 0–100 | 1 | 25 |
| Sand weight | 0–100 | 1 | 3 |
| Grass weight | 0–100 | 1 | 75 |
| Desert weight | 0–100 | 1 | 0 |
| Smoothing | 1–8 | 1 | 6 |
| Lake density | 10–50 | 5 | 25 |
| Lake size | 10–40 | 5 | 25 |
| Fruit | 0–64 | 1 | 4 |
| Repeat landscape | 1–32 | ×2 | 1 |

## Concrete islands

| Control | Range | Step | Default |
|---|---|---:|---:|
| Channel width | 5–8 | 1 | 5 |
| Extra islands | 0–6 | 1 | 3 |

## Isles

| Control | Range | Step | Default |
|---|---|---:|---:|
| Island size | 45–65 | 5 | 60 |
| Land bridge width | 3–6 | 1 | 4 |

## Shattered Coast

| Control | Range | Step | Default |
|---|---|---:|---:|
| Water weight | 0–100 | 1 | 40 |
| Sand weight | 0–100 | 1 | 4 |
| Grass weight | 0–100 | 1 | 60 |
| Smoothing | 1–8 | 1 | 3 |

## Rugged Archipelago

| Control | Range | Step | Default |
|---|---|---:|---:|
| Island size | 50–70 | 1 | 65 |
| Beach size | 0–4 | 1 | 1 |
