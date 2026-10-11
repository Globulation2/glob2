# Final retained GL/legacy comparison

Frozen5adb2b3fa→7c8c18c20; three rounds,36 processes,14760 completed intervals. SDL second-pass reverted; rejected920 evidence preserved separately.

| Workload | Backend | Wall p50 ms before→after | Wall p95 ms before→after | Wall p99 ms before→after | CPU p50 ms before→after | Wall p50 ratio/MAD |
|---|---|---:|---:|---:|---:|---:|
| fractional 2D clouds | legacy-gl | 9.544→9.486 | 11.006→11.292 | 12.902→12.399 | 9.218→9.161 | 0.996/0.003 |
| fractional 2D no clouds | legacy-gl | 2.730→2.647 | 3.653→3.590 | 4.436→4.971 | 2.996→2.930 | 1.013/0.015 |
| fractional 2D clouds | programmable-gl | 12.771→12.698 | 14.871→14.785 | 16.044→16.252 | 11.590→11.634 | 0.993/0.000 |
| fractional 2D no clouds | programmable-gl | 4.355→4.364 | 5.774→5.431 | 6.906→6.420 | 3.956→3.980 | 1.002/0.000 |
| native 2D clouds | legacy-gl | 2.411→2.309 | 2.982→2.891 | 3.214→3.063 | 1.959→1.836 | 0.963/0.009 |
| native 2D no clouds | legacy-gl | 0.473→0.465 | 0.629→0.652 | 0.697→0.944 | 0.325→0.318 | 0.987/0.002 |
| native 2D clouds | programmable-gl | 3.118→2.995 | 3.794→3.646 | 4.032→3.906 | 2.402→2.276 | 0.990/0.049 |
| native 2D no clouds | programmable-gl | 0.467→0.464 | 0.598→0.599 | 0.722→0.832 | 0.226→0.209 | 0.980/0.006 |
| torus Torus no clouds | legacy-gl | 6.084→6.150 | 6.793→7.336 | 6.961→7.809 | 4.872→4.886 | 1.020/0.020 |
| torus Torus no clouds | programmable-gl | 16.068→15.988 | 19.860→18.249 | 19.867→21.412 | 9.977→9.871 | 1.002/0.002 |

No broad speedup or default qualification. Every >5%flag is preserved in qualification.json with absolute values and per-round ratios/MAD.

Three rotating rounds only; torus30/4, not frozen7×300 qualification.
No SDL new performance claim (second-pass reverted); no software/WebGPU/cold/stream/UI/recording performance.
Synchronous1x1readback/holds/retries included, finalpresentation excluded; CPU is wholeprocess, noGPUtimestamp.
UserFlatpak continues; agentownedGPUworkloadslimitedtoonebenchmark.
Every >5%tailflag retained. No broad speedup/default qualification; no gate widening.

Native-off GL wallp99 perround0.679/0.748/0.722→0.821/0.832/0.948ms. SubmitCPU p500.057/0.057/0.059→0.047/0.044/0.047ms; p990.094/0.097/0.119→0.103/0.078/0.081ms. Capture-wait-inclusive p990.537/0.565/0.555→0.696/0.653/0.832ms. No changed-pathCPU slowdown isolated; capture/walltailflag remains, GPU/scheduling attribution unproven.
