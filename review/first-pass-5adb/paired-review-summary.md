# First-pass renderer comparison

Frozen before aeca7b34c; after 5adb2b3fa. Same full release fixture,1280×800, SDL3.4.16 and runtime artwork. Three rotating rounds;54 processes,22140 completed measured intervals.

| Workload | Backend | Wall p50 before→after ms | CPU p50 before→after ms | Wall p50 ratio | Wall p95 ratio | Wall p99 ratio | Ratio MAD |
|---|---|---:|---:|---:|---:|---:|---:|
| fractional 2D clouds | legacy-gl | 9.136→9.273 | 8.858→8.985 | 1.015 | 1.052 | 1.022 | 0.002 |
| fractional 2D no clouds | legacy-gl | 2.430→2.474 | 2.705→2.742 | 1.027 | 1.033 | 0.976 | 0.007 |
| fractional 2D clouds | programmable-gl | 15.056→12.470 | 14.009→11.378 | 0.817 | 0.851 | 0.847 | 0.002 |
| fractional 2D no clouds | programmable-gl | 4.348→4.187 | 3.987→3.788 | 0.956 | 0.939 | 0.911 | 0.003 |
| fractional 2D clouds | sdl-gpu | 98.927→14.946 | 80.948→15.175 | 0.151 | 0.177 | 0.203 | 0.012 |
| fractional 2D no clouds | sdl-gpu | 34.198→5.719 | 20.727→5.771 | 0.167 | 0.178 | 0.202 | 0.005 |
| native 2D clouds | legacy-gl | 2.374→2.329 | 1.861→1.815 | 0.981 | 1.020 | 0.989 | 0.000 |
| native 2D no clouds | legacy-gl | 0.459→0.463 | 0.314→0.317 | 1.011 | 0.933 | 0.931 | 0.002 |
| native 2D clouds | programmable-gl | 3.841→2.989 | 3.109→2.241 | 0.768 | 0.766 | 0.747 | 0.027 |
| native 2D no clouds | programmable-gl | 0.489→0.455 | 0.239→0.216 | 0.940 | 0.957 | 0.943 | 0.001 |
| native 2D clouds | sdl-gpu | 31.517→3.102 | 18.146→2.821 | 0.098 | 0.114 | 0.136 | 0.006 |
| native 2D no clouds | sdl-gpu | 0.611→0.448 | 0.284→0.247 | 0.734 | 0.721 | 0.843 | 0.016 |
| torus Torus no clouds | legacy-gl | 6.040→5.906 | 4.758→4.604 | 0.982 | 1.068 | 0.968 | 0.004 |
| torus Torus no clouds | programmable-gl | 38.172→15.558 | 13.766→9.370 | 0.405 | 0.439 | 0.356 | 0.001 |
| torus Torus no clouds | sdl-gpu | 68.161→21.762 | 43.973→15.582 | 0.318 | 0.326 | 0.276 | 0.005 |

Torus GL holds2160→542; SDL holds2160→540,90 completions per family/version. No ordinary admission deferrals.

Three rounds instead of frozen seven-round gate. Torus30 measured/4warmup per process, not300/30.
No cold loading, streaming, editor/UI, recording performance or GPU timestamp measurements.
Wall includes all holds, retries and1x1 synchronous readback; final presentation excluded. Process CPU is entire process CPU.
Concurrent user Flatpak app remains active. Ratio/MAD shows paired observed noise; sample count and background contention limit qualification.
Legacy p95 regressions exceed five-percent ceiling; do not enlarge gates or claim universal improvement.
aeca→5adb includes intervening master changes; identical fixture/checksum/runtime assets and pins do not isolate every source change.

Six flat round-zero before/after images are pixel-exact. Torus phase/turn-dependent images differ and remain auxiliary evidence. No software or WebGPU timing in this native scope.
