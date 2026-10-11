Final native paired release comparison

Production aeca7b34c, base c3cb01070; the same full-profile executable and fixture run across all backend families. Window 1280×800; map viewport 1120×800; fractional zoom 0.136719 and native zoom 1.0. All 51 processes passed; checksum 86fc2e1c matches.

Median of per-run quantiles, milliseconds. W=completionwall,C=processCPU.

| Workload/mode | Backend | W p50/p95/p99 | C p50/p95/p99 | W p50 run MAD | Completed/attempts/holds |
|---|---|---|---|---|---|
| flat-warm 2D clouds | legacy-gl | 9.411/10.961/11.667 | 9.107/9.958/10.470 | 0.58% | 2100/2100/0 |
| flat-warm 2D clouds | programmable-gl | 15.487/18.021/18.994 | 14.364/15.675/16.791 | 0.48% | 2100/2100/0 |
| flat-warm 2D clouds | sdl-gpu | 101.379/108.066/115.089 | 85.168/94.370/101.551 | 0.14% | 2100/2100/0 |
| flat-warm 2D no clouds | legacy-gl | 2.655/3.526/4.439 | 2.921/3.510/3.783 | 4.51% | 2100/2100/0 |
| flat-warm 2D no clouds | programmable-gl | 4.675/5.557/6.021 | 4.302/5.036/5.429 | 1.01% | 2100/2100/0 |
| flat-warm 2D no clouds | sdl-gpu | 35.082/37.634/45.861 | 22.070/24.535/25.775 | 0.32% | 2100/2100/0 |
| flat-warm-native 2D clouds | legacy-gl | 2.365/2.860/3.216 | 1.883/2.423/2.649 | 0.97% | 2100/2100/0 |
| flat-warm-native 2D clouds | programmable-gl | 3.998/4.744/5.402 | 3.235/3.932/4.275 | 1.02% | 2100/2100/0 |
| flat-warm-native 2D clouds | sdl-gpu | 31.891/34.056/37.704 | 18.793/21.436/24.756 | 0.77% | 2100/2100/0 |
| flat-warm-native 2D no clouds | legacy-gl | 0.468/0.606/0.692 | 0.320/0.449/0.536 | 1.94% | 2100/2100/0 |
| flat-warm-native 2D no clouds | programmable-gl | 0.505/0.622/0.712 | 0.254/0.365/0.414 | 1.70% | 2100/2100/0 |
| flat-warm-native 2D no clouds | sdl-gpu | 0.628/0.801/0.918 | 0.307/0.468/0.595 | 0.83% | 2100/2100/0 |
| torus Torus no clouds | legacy-gl | 6.123/9.643/13.139 | 4.779/5.675/11.863 | 0.25% | 90/90/0 |
| torus Torus no clouds | programmable-gl | 38.695/40.353/41.715 | 14.423/16.235/17.442 | 0.01% | 90/2250/2160 |
| torus Torus no clouds | sdl-gpu | 70.058/85.146/91.341 | 45.985/60.776/67.606 | 0.69% | 90/2250/2160 |

Paired same-round Legacy ratios (1.0=equal), relative MAD in parentheses.

| Workload/mode | Candidate | W p50/p95/p99 ratio | C p50/p95/p99 ratio |
|---|---|---|---|
| flat-warm 2D clouds | programmable-gl | 1.640(0.6%)/1.625(2.6%)/1.618(3.5%) | 1.579(0.7%)/1.577(2.3%)/1.620(2.7%) |
| flat-warm 2D clouds | sdl-gpu | 10.786(1.1%)/9.757(2.3%)/9.449(2.1%) | 9.340(0.7%)/9.477(1.8%)/9.828(5.0%) |
| flat-warm 2D no clouds | programmable-gl | 1.751(5.6%)/1.663(6.9%)/1.441(5.2%) | 1.462(4.3%)/1.459(2.4%)/1.396(2.8%) |
| flat-warm 2D no clouds | sdl-gpu | 13.234(4.6%)/12.069(10.8%)/11.098(21.4%) | 7.556(3.9%)/6.988(2.4%)/6.760(5.9%) |
| flat-warm-native 2D clouds | programmable-gl | 1.687(1.8%)/1.571(5.6%)/1.641(3.5%) | 1.705(2.2%)/1.623(3.1%)/1.614(1.7%) |
| flat-warm-native 2D clouds | sdl-gpu | 13.486(1.7%)/11.618(3.6%)/12.225(19.5%) | 9.980(2.5%)/8.371(6.4%)/8.952(10.5%) |
| flat-warm-native 2D no clouds | programmable-gl | 1.095(1.2%)/1.008(1.6%)/1.049(6.0%) | 0.796(2.7%)/0.806(2.6%)/0.855(10.5%) |
| flat-warm-native 2D no clouds | sdl-gpu | 1.338(3.0%)/1.317(5.1%)/1.343(3.5%) | 0.937(3.0%)/1.007(4.2%)/1.120(10.4%) |
| torus Torus no clouds | programmable-gl | 6.319(0.2%)/4.185(19.5%)/3.175(26.2%) | 2.995(5.2%)/2.789(1.6%)/1.470(13.9%) |
| torus Torus no clouds | sdl-gpu | 11.637(0.1%)/7.654(5.0%)/5.820(1.1%) | 9.720(1.4%)/10.885(0.4%)/5.726(22.6%) |

Recommendation: retain defaults; modern backends remain opt-in. Fractional and cloud regressions far exceed the 5% ceilings, even where noise is low. Native cloud-off GL process CPU benefit does not clear the wall gate. No default qualification.

Coverage: 25,200 primary completed intervals +270 representative torus intervals; 4,320 measured intentional torus hold turns; zero ordinary admission deferrals. Modern torus retained 4096²/density16; it retains the previous completed main image while bounded capture progresses. The full 7×300 torus gate is UNQUALIFIED.

Same executable alternative backends, not pre-migration versus new-version benefit.
Concurrent unrelated user Flatpak glob2 PID1611240 retained; no uncontended-host inference.
Wall/processCPU interval includes tiny synchronous readback and all discard/1msyield/hold/retrycost, excludesfinal presentation.
No GPU timestamp timings.
Torus3×30 does not satisfy frozen7×300 gate; nativezoom1 workload hasno originalP0 GPUbenchmark gate.
Cold/streaming/UI/interactive-recording performance omitted.
Unsupportedhardwareplatforms remain opt-in; no nativeWindows/Metal hardware execution.

sdl-gpu-capability-post-timing.log preserves the actual SDL device-property API result. Each baseline.json and raw run log retains requested/active backend, counters, commands, toolchain/hardware inventory and hashes. qualification.json includes full case/gate receipts. Frozen source/native-equivalence inputs are included separately.
