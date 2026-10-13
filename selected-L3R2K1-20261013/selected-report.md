# Selected turret configuration: L=3, R=2, K=1, margin=1

The maintainer selected this configuration and requested consolidation into normal-game targeting, explicitly authorizing a save/replay compatibility break and simulation-version advance. Selection is a maintainer decision, not a claim that all original engineering/live gates passed.

Source: 76cc03a69a9612b245f44b8bce4d322113b132d3; base 7e9d63af602e6f3bf970ddff951fd2d0da0be35b; fetched master 277510840054b1416f2defd7d86fe1431dd0ddc0. Linux x86_64, GCC13.3, -O3 release, native SDL3 prefix. Existing Numbi/Castor fixtures, ten seeds, two layouts, 1024ticks each. Actual shared snapshot/executor producer with cooldown admission; original bullets authoritative in shadow. Twenty paired stock battles per setting. All original/shadow AI orders, per-tick checksums and final saves match byte-for-byte.

| L | R | K | Target/aim agreement | Missed windows | Unresolved windows | Combat p95 delay | Max completed delay |
|---|---|---|---|---|---|---|---|
|8|1|1|758/868 (87.33%)|2/868 (0.23%)|0|0ticks|0ticks|
|8|2|1|755/868 (86.98%)|2/868 (0.23%)|0|0ticks|1tick|
|2|2|1|734/868 (84.56%)|11/868 (1.27%)|1|0ticks|9ticks|
|**3**|**2**|**1**|**747/868 (86.06%)**|**5/868 (0.58%)**|**0**|**0ticks**|**5ticks**|
|4|2|1|753/868 (86.75%)|2/868 (0.23%)|0|0ticks|5ticks|

Misses count as infinite added delay, not excluded passing samples. P95 does not describe the worst tail. Controlled stock birth/approach tests across all admission phases: L3/R2/K1 has 14reference opportunities, zero missed first windows, p95 added delay2ticks. These controls use serial shortlists; they are distinct from actual-producer combat. The earlier one-tick air adverse cohort remains failed; no universal responsiveness claim is made. Shadow evidence describes the original trajectory, not the battle outcome of different shots.

Validation: 159,000assertions per stock configuration; L2/L3/L4 cached traces deterministic across1/2/4/8participants (9,216assertions each); same committed source passes59focused TurretTargeting/WorldSnapshot/SharedWorkerLifecycle cases,202,626assertions. Final committed L2trace run passes9,216assertions. Pure planner bounded-prefix coverage now includes2/3entries. Selected L3stock run repeated on the committed release build; raw producer CSV and reference/shadow proofs retained in selected-stock/.

Performance limits: the maintainer stopped the six-configuration paired CPU sweep before completion. All partial owner measurements are retained with host overlap, but no final L3 CPU delta, memory minimum, or whole-engine speedup is established. The previous complete L8/R1/K1 study remains a separate baseline: sustained-combat elapsed+3.15%, processCPU+1.52% versus original targeting; those numbers must not be attributed to L3/R2/K1. The paused/interrupted earlier sweep and every recorded sample remain in the archive; no measured-cost outliers were removed.

Pre-consolidation save continuation is not qualified: L2/R2 cached continuation fails cuts38/39/40; original/shadow9/9each. Cached lists/admissions are not yet persisted. Normal-game integration must add deterministic continuation to its new save format despite the authorized old-format rejection. No live experiment was enabled in this measured revision. Cross-platform execution comparisons, new-version replay/network acceptance and golden regeneration remain outstanding integration work.

Shipping targeting and hiring are unchanged in this evidence revision. Old source and frozen benchmark binaries are retained here so deleting the legacy selector does not erase the reference evidence.
