# How often does a tick need unit details?

Measured source `fdca48059` (full SHA in metadata), base `277510840054b1416f2defd7d86fe1431dd0ddc0`. The two-layer core/detail layout is unchanged; no presentation subdivision or production movement adoption.

**The six real Cortex/Nicowar/Warrush games requested details on every advancing gameplay tick under their current observation contracts.** Headless Null-AI movement-shadow runs required cores only on every advancing tick. Rendering without selection also uses cores only; selecting a unit adds details. Cortex already requires details, so selection adds no further unit capture in that case.

The counters measure declared snapshot requirements, not per-field access tracing. A full request does not establish that detail fields are actually read, or dynamically necessary, on every tick.

## Observed sessions

The following percentages use every distinct logical tick observed by the shared snapshot store, including initial/final observations. Requirements are normalized and ORed across all same-tick requests. Details imply cores. Repeated requests and requirement upgrades never multiply the tick denominator.

| Session | Observed ticks | Core only | Core + details | No unit requirement |
|---|---:|---:|---:|---:|
| ai-river-1 | 12,000 | 0.000% | 99.992% | 0.008% |
| ai-river-2 | 12,000 | 0.000% | 99.992% | 0.008% |
| ai-river-3 | 12,000 | 0.000% | 99.992% | 0.008% |
| ai-symmetric-arena-1 | 12,000 | 0.000% | 99.992% | 0.008% |
| ai-symmetric-arena-2 | 12,000 | 0.000% | 99.992% | 0.008% |
| ai-symmetric-arena-3 | 12,000 | 0.000% | 99.992% | 0.008% |
| gui-cortex-colony-selected | 545 | 0.000% | 100.000% | 0.000% |
| gui-cortex-colony-unselected | 545 | 0.000% | 100.000% | 0.000% |
| gui-large-colony-selected | 545 | 0.000% | 98.716% | 1.284% |
| gui-large-colony-unselected | 545 | 98.899% | 0.000% | 1.101% |
| gui-ordinary-control-selected | 545 | 0.000% | 100.000% | 0.000% |
| gui-ordinary-control-unselected | 545 | 100.000% | 0.000% | 0.000% |
| headless-large-colony | 1,025 | 99.902% | 0.000% | 0.098% |
| headless-large-colony-no-shadow | 1,025 | 0.000% | 0.000% | 100.000% |
| headless-ordinary-control | 1,025 | 99.902% | 0.000% | 0.098% |
| headless-ordinary-control-no-shadow | 1,025 | 0.000% | 0.000% | 100.000% |

![Requested components per logical tick](unit-view-ticks.png)

Six headless AI runs load existing real-game states at tick 1 and advance to 12,000: 11,999 detail-requesting gameplay ticks per game, plus one final checksum-preparation observation with no unit requirement. The two headless Null-AI shadow fixtures advance from zero to 1,024: 1,024 core-only ticks, plus the checksum-preparation observation. This explains 99.992%/99.902% in the raw totals; it is not a rare gameplay tick that avoided the corresponding requirement. The terminal checksum-boundary mask and call stack were checked separately with GDB, using the Linux x86-64 return-value ABI and compiler-derived field offsets. Failed initial debugger setup attempts are retained but not used as evidence.

The six GUI runs are actual concurrent Engine sessions, 32 warmup + 512 measured advances at normal 30 TPS, presentation target 60 FPS, software/Xvfb at 640x480, two executor participants. Their 545 observed ticks include the initial world and the final tick. The 1,024-unit unselected colony requests cores on 539 ticks; the selected run requests details on 538. On the remaining ticks presentation preparation has not admitted a new unit-bearing observation. GUI admission depends on readiness and pacing, so these exact frequencies describe these sessions under recorded host load, not a universal distribution of human selection behavior. Ordinary GUI controls request their respective components on all 545 ticks. Cortex requests details on all 545 ticks, selected or unselected.

## Requests versus actual copying

The compact core is 132 authoritative bytes / 160 bytes with snapshot metadata. Details add 216 authoritative bytes per unit. The table counts actual component capture operations separately from requests; authoritative state bytes exclude indexes, relationship metadata, cargo sidecars and retained capacity.

| GUI case | Request calls | Core captures | Detail captures | Core state bytes | Detail state bytes |
|---|---:|---:|---:|---:|---:|
| gui-cortex-colony-selected | 1,088 | 545 | 545 | 73,666,560 | 120,545,280 |
| gui-cortex-colony-unselected | 1,088 | 545 | 545 | 73,666,560 | 120,545,280 |
| gui-large-colony-selected | 1,088 | 538 | 538 | 72,720,384 | 118,996,992 |
| gui-large-colony-unselected | 1,088 | 539 | 0 | 72,855,552 | 0 |
| gui-ordinary-control-selected | 1,088 | 545 | 545 | 2,302,080 | 3,767,040 |
| gui-ordinary-control-unselected | 1,088 | 545 | 0 | 2,302,080 | 0 |

The unselected large colony copies **zero detail bytes**, versus approximately 119 MB in the selected run. At an equal unit/capture count, core-only authoritative copying omits 216 of 348 bytes (**62.07%**); the populated record payload omits 216 of 376 bytes (**57.45%**). These are unit byte ratios, not overall engine/runtime improvements. Actual session volumes differ slightly because presentation admission differs.

Cortex/Nicowar/Warrush request `Simulation` (with selected components removed in Warrush), which includes the normalized `Entities` aggregate and therefore details. The migrated unit-under-attack/defense scans still read compact cores; other AI decisions continue using details. **0% core-only does not mean no compact-core use.** It means details were also needed somewhere in that tick. Rendering and AI reuse the same captured cores; a selected panel on top of a full AI observation does not force another copy. Null-AI headless controls without shadow request no unit records on any of their 1,025 observed ticks. Building-only requests remain independent of unit requirements.

## Verification and limits

127 focused engine cases pass, one display-tagged case skipped by the explicit headless selection. Coverage includes snapshot union/upgrades/reset, compact records, planner delays/participants, scene extraction, gradients and affected AI observation cases. Release controls at two and four participants preserve per-tick checksums, replay bytes, replay checksum streams and decompressed final saves against the original reference. An initial control omitted `team-timeline`: it preserved checksums/replay but differed in 22 stored counter bytes (and the dependent SHA1). Restoring identical telemetry options makes the save byte-identical at both participant counts. The mismatched control is retained and is not used as a save-equivalence claim. All six measurement runs preserve reference initial/final checksums; they do not all record per-tick checksums.

No new runtime dependencies. No save/replay/network/SIM version change. Linux x86-64/GCC 15.2 only; macOS, Windows, browsers, GPU backends, human multiplayer/selection patterns and longer UI sessions are unavailable or unmeasured. Concurrent tournament activity is recorded; no new clean FPS/TPS or capture-time performance claim is made from these frequency runs. Prior reserved-core runtime evidence remains separate. Original movement gates are unchanged, production remains Stage A and disabled by default.

[Exact protocol](unit-view-protocol.md) · [Raw samples](unit-view-samples.csv) · [Metadata](unit-view-metadata.json) · [Commands/logs/fixtures/debugger controls](unit-view-evidence.zip)
