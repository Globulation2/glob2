# Final zone-gated, filtered-model comparison

All modern arms use threshold24, minimum refresh spacing32, publication delay8,
H128 labels and a demanded-field age cap1024. Four arms separate the complete PR
from the new empty-zone gate and the refit. Master retains its historical policy.

| Arm | CPU ms/measured tick | Sim CPU s | Process CPU s | Wall s | Measured ticks | Gross ticks/s | Measured ticks/s | Periodic jobs | Lazy jobs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| master_reference | 0.154532 | 10.127384 | 12.739006 | 13.979054 | 65536 | 4834.7 | 4688.2 | 67584 | 19708 |
| 184_unfiltered_no_zone_gate | 0.123822 | 8.114800 | 10.683833 | 11.168395 | 65536 | 6051.4 | 5868.0 | 12636 | 17172 |
| 185_unfiltered_zone_gate | 0.121627 | 7.970970 | 10.539411 | 11.577065 | 65536 | 5837.7 | 5660.8 | 12556 | 17172 |
| 185_filtered_zone_gate | 0.121302 | 7.949630 | 10.500764 | 11.503107 | 65536 | 5875.3 | 5697.2 | 12236 | 16868 |

Each arm advanced67,584 ticks in eight processes, including256settling ticks per
process;65,536ticks contributed to the CPU metric. Job totals, processCPU and wall
include loading and settling; simulationCPU excludes settling.

The final build uses21.50% less CPU per measured tick than matched master, with
four paired-repeat range20.18–23.50% less. Versus184 it uses2.04% less (paired range
−7.49% to+2.79%); versus the185 unfiltered-model control it uses0.27% less (−3.04%
to+4.05%), effectively flat locally. The complete PR/master difference includes
all changes and resulting trajectories; it is not pure model-intelligence savings.

Removing zero-zone queries changes quality denominators.184→185unfiltered leaves
raw extra-cost totals and stall counts unchanged in these panels, while removing
90Guard observations in4FFA and47Guard+106Clear observations in8FFA. Higher
aggregate extra-cost/query and stall percentages there are denominator effects,
not additional raw errors. General trajectory or unit-wait effects are not ruled
out by this limited panel.

| State | Arm | Queries | Extra total | Extra/tick | Extra/query | FalseUNR count | Missed move count | Stalls | Fresh-correctable stalls | Planning queries / FN / FP |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 4ffa | 184_unfiltered_no_zone_gate | 1407 | 1645 | 0.200806 | 1.169 | 7 | 21 | 7 | 4 | 33650 / 104 / 101 |
| 4ffa | 185_unfiltered_zone_gate | 1317 | 1645 | 0.200806 | 1.249 | 7 | 21 | 7 | 4 | 33650 / 104 / 101 |
| 4ffa | 185_filtered_zone_gate | 1513 | 1352 | 0.165039 | 0.894 | 3 | 13 | 5 | 4 | 66224 / 126 / 123 |
| 8ffa | 184_unfiltered_no_zone_gate | 1461 | 1170 | 0.142822 | 0.801 | 9 | 34 | 29 | 16 | 2649 / 4 / 152 |
| 8ffa | 185_unfiltered_zone_gate | 1308 | 1170 | 0.142822 | 0.894 | 9 | 34 | 29 | 16 | 2649 / 4 / 152 |
| 8ffa | 185_filtered_zone_gate | 1308 | 1170 | 0.142822 | 0.894 | 9 | 34 | 29 | 16 | 2649 / 4 / 152 |

The refit comparison is185unfiltered→185filtered. In4FFA extra cost decreases
1645→1352 (17.81%), stalls7→5 and missed moves21→13; fresh-correctable stalls remain4.
Planning errors increase in absolute counts while query count nearly doubles, so
neither those rates nor movement rates describe a fixed query population.8FFA
quality counts are unchanged. This is mixed, limited prospective evidence rather
than universal dominance. Remaining stalls are not automatically proven congestion.

Per-family movement counts, raw outcomes, planning outcomes and work reasons are
in SUMMARY.json. Captured-goal episode support is not raw painted-zone history.
Markets is unmeasured and uses the combined fallback; no fallback-cost or individual
feature/counter causality is claimed.

Four Williams-balanced repeats on two previously examined175 saved worlds provide
local paired spread, not a population confidence interval or independent broad
validation. No timing overlapped compilation. Process/load/thermal/power records
are retained, and no uncontended physical-core claim is made. Every repeated
arm/world pair and observerON comparison matched initial/final checksums.

PLAN.json binds exact binary/input identities. SOURCE-QUALIFICATION.json resolves
the full final production4285ca7ea6db02cd114d10692074e9fa7796f06a and fixture-only
5f0f source, and points to the actual macos185 qualification receipt. The frozen
PLAN uses the operator shorthand filtered185 directory label; that label is not
an additional qualification run. STATUS.json retains every raw measurement.
