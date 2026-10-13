# Compact unit core evidence

## Complete evidence index

Source changes are committed and pushed through `fdca48059e632268f0b814abba25afc60e2370f0`; final tested master base is `277510840054b1416f2defd7d86fe1431dd0ddc0`. Each study records its own frozen revisions, inputs, toolchain, commands and limitations. Do not pool timings from different revisions or interpret faster shadow computation as faster adopted movement.

| Study | Report and reviewer-accessible evidence | Main finding |
|---|---|---|
| Initial movement fixes and gate retest | [Report](https://github.com/Globulation2/glob2/blob/87acba7093d90b2c308a3cade9788360be56d7e9/report.md) · [Samples, profiles and correctness](https://github.com/Globulation2/glob2/blob/87acba7093d90b2c308a3cade9788360be56d7e9/movement-fixes-evidence.zip) | Duplicate captures/map-view construction/cache fragmentation fixed; one-input shadow runtime improves 65.56%, but every delay still fails a gate somewhere. |
| Deep optimized Callgrind and causal probes | [Report](https://github.com/Globulation2/glob2/blob/50bd31217c1c8bab46ab6434b45e7cdf7d5c6552/report.md) · [Profiles, annotations and probes](https://github.com/Globulation2/glob2/blob/50bd31217c1c8bab46ab6434b45e7cdf7d5c6552/deep-analysis-evidence.zip) | Real walkability changes dominate rebuilds. Ordered admission/effective-goal sharing remain separate candidates; contaminated native timings are not used for performance claims. |
| Compact records, shared consumers and independent/combined capture | [Report](report.md) · [Artifacts](compact-core-evidence.zip) | 160-byte core view meets 192-byte budget; guard uses cores, standalone market copies no unit state; saves/checksums/scene/gradient controls retained. |
| Capture-inclusive runtime and panel investigation | [Report](runtime-report.md) · [Artifacts](runtime-evidence.zip) | Large-colony fresh-capture pipelines improve; reuse controls, ordinary/panel negative results, longer replication and Callgrind attribution are preserved. |
| Reserved-core concurrent live FPS/TPS/CPU | [Report](live-report.md) · [Artifacts](live-evidence.zip) | Normal large-colony CPU improves 1.20% (95% CI 0.75–1.76%); both hold 60 FPS/30 TPS. Uncapped FPS/max TPS benefit is inconclusive. |
| Original real-game direction agreement, delays 0–8 | [Report](agreement-report.md) · [Metrics/traces](agreement-evidence.zip) | Six games, 54 canonical comparisons; approximately 39% agreement exposed policy/validation/admission problems. Per-game initial saves, replays and checksums are linked in the report. |
| Revised policy agreement and disagreement attribution | [Report](deep-report.md) · [Artifacts](movement-deep-evidence.zip) | K=1 agreement 78.46%, coverage 86.96%; 60 canonical comparisons pass. Randomness/conflicts/staleness are distinguished; remaining field-cost labels are not an independent route oracle. |
| Full aggregate conditional-step gameplay tournament | [Report](tournament-report.md) · [Statistics](tournament-statistics.json) · [Metadata](tournament-metadata.zip) | 240/240 games complete. Food/worker-time −30.36%, starvation/worker-time +66.19%, worker moves/food delivery +19.20%, Holm-significant. Fail: do not adopt. Eight generator archives retain every game's telemetry, maps and commands. |
| Unit requirement frequency and final integration controls | [Report](unit-view-report.md) · [Samples](unit-view-samples.csv) · [Artifacts/JUnit](unit-view-evidence.zip) | Real AI contracts request details every advancing tick; unselected rendering avoids details. 127 focused cases pass, one display skip; matched two/four-participant canonical controls pass. |

The frequency instrumentation measures **declared component requests**, not actual per-field reads. Full AI request masks do not prove every detail field is dynamically necessary every tick. Copy counts, populated payload, retained capacity, field work, instruction counts, elapsed time and whole-process CPU are separate measurements.

All original gates remain unchanged. Production movement is unchanged and disabled by default; the separately patched adopted-step cohort is research-only and shows regressions. There is no qualified net movement concurrency benefit, independent proof of route quality, complete workload/allocator-inclusive 160 MiB qualification or adopted save/replay/network coverage. macOS, Windows, browser variants, GPU/human-play/footage coverage remain unavailable or unverified. Earlier baseline failures and failed measurement/debugger setups are retained and identified by their reports.

[Unit view frequency measurements](unit-view-report.md) · [Raw samples](unit-view-samples.csv) · [Commands and controls](unit-view-evidence.zip)

Six real AI matches need details on every advancing gameplay tick. Null-AI movement-only runs need cores only on every advancing tick. The unselected 1,024-unit GUI colony is core-only on 98.9% of observed ticks; selecting a unit requests details on 98.7%. Cortex GUI sessions need details on all ticks regardless of selection. Requests are unioned within a tick; copy counts/bytes are separate.

![Unit requirements per logical tick](unit-view-ticks.png)

[Movement disagreement investigation](deep-report.md) · [Traces and controls](movement-deep-evidence.zip) · [240-game research tournament](tournament-report.md)

The revised K=1 shadow policy reaches 78.46% exact agreement and 86.96% comparison coverage on the same six reference histories; all 60 reference comparisons preserve canonical outputs. **The separately patched conditional-step tournament fails:** food/worker-time −30.36%, starvation/worker-time +66.19%, worker moves/food-delivery +19.20%, significant after Holm correction. Do not adopt this policy. Production remains shadow-only, disabled by default; original gates are unchanged. No new net runtime benefit is claimed for the added completion refresh.

![Gameplay intervals](tournament-effects.png)

[New reserved-core live match comparison](live-report.md) · [Live raw evidence](live-evidence.zip)

Ten paired release repetitions per scenario, with concurrent simulation and software presentation: the normal 1,024-unit colony uses about **1.2% less game CPU** (95% CI 0.75–1.76%). Capture is about **35% cheaper**, with **36% less logical capture payload**. Both variants hold 60 FPS/30 TPS; uncapped FPS and maximum TPS gains are inconclusive. The full-observation Cortex case and ordinary control are reported separately. CPU restoration audits, all paired samples and byte-identical live replay/checksum evidence are included. Linux software/Xvfb coverage does not establish GPU/desktop-display performance.

![Live benefit confidence intervals](live-benefit.png)


[New capture-inclusive runtime follow-up](runtime-report.md) · [Runtime raw evidence](runtime-evidence.zip)

Twenty paired GUI repetitions and a separate longer-window replication establish a large-colony capture-inclusive pipeline benefit. The report preserves ordinary/panel limitations and distinguishes this from live-match frame rates.

![Capture attribution and measured pipeline costs](capture-runtime-phases.png)


[Report and limitations](report.md) · [Raw evidence archive](compact-core-evidence.zip) · [Source/platform metadata](source-state.json)

![Record, capture and Callgrind comparisons](compact-core-evidence.png)

![Engine and drawing confidence intervals](compact-runtime-intervals.png)

The timings compare frozen revisions; final integration checks are separately identified in the report. Movement remains Stage A. The archive includes raw commands, samples, profiles, JUnit XML, baseline reproductions, saves/replays/checksums and corrected software images. Reproduced baseline failures and unavailable platform/gameplay coverage remain explicit.


[Real-game direction agreement and delay sweep](agreement-report.md) · [Raw metrics/traces/commands](agreement-evidence.zip)

Six active AI games, diagnostic delays 0 through 8, 54 matched reference comparisons. Exact agreement is 38.99% to 38.77%; usable coverage is 56.45% to 55.81%. Resource steering agrees about 75%, while building comparison coverage is poor and requires validation-semantic investigation. These are conditional steps along unchanged reference histories, not adopted-policy gameplay. The report links deduplicated initial saves, replays and checksums for every game. The core/detail layout remains unchanged.

![Direction agreement and comparison coverage versus delay](agreement-delay.png)
