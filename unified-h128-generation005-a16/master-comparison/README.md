# Current master versus gen005 CPU comparison

Master `84edb385cd740c3979c06ef901ab4871ea8115c2` (SIM77/save175), gen005 `4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e` (SIM90/save180, threshold16).

CPU only; 256 unmeasured settling ticks after common master175 save, then8192 measured ticks. Process CPU/wall cover full8448+setup; primary engine CPU excludes initial256. No trace/probe/work/compute telemetry.

|State|Master mean CPU ms/tick|Gen005 mean CPU ms/tick|Change|Master median [range]|Gen005 median [range]|
|---|---:|---:|---:|---:|---:|
|4ffa|0.158359|0.115605|-27.00%|0.158719 [0.156626,0.159373]|0.115810 [0.114667,0.116133]|
|8ffa|0.139224|0.116761|-16.13%|0.139286 [0.138465,0.139858]|0.116931 [0.115780,0.117401]|

Pooled primary simulation CPU/tick: 0.148792→0.116183ms (-21.92%).

Per-arm totals: 65536 measured ticks, 67584 actual ticks. Simulation CPU 9.751202→7.614160s; process CPU 12.298884→10.087029s; wall 13.299964→10.589452s. Pooled actual-tick throughput 5081.52→6382.20 ticks/s (sum actual ticks / sum wall).

CPU changes compare means; medians and repeat ranges are separately shown. Positive CPU changes mean gen005 used more CPU. Whole-game trajectories may diverge; this is not an isolated coefficient effect.

Within-arm repeated initial/final checksums and actual/measured ticks exact; cross-arm equality not expected.

- Three corresponding already examined generator/map/roster specs; regenerated with current master, not a fresh representative sample
- Common states follow master gameplay through16384; prospective continuation trajectories can diverge
- Comparison is full current master77 versus candidate90, including all code differences, not a model-only coefficient effect
- Mac OS scheduler may migrate cores; ambient load and available pmset diagnostics recorded, no cold/uncontended or continuous thermal-control claim
- No local training; remote collection/coordinators continue
- Four timing repeats quantify repeat spread, not uncertainty over map population
- No ON/quality expansion; CPU-only requested comparison

The predeclared2v2 bootstrap ended at tick14850 before the16384 warm boundary and was retained/skipped without replacement. The report therefore covers two surviving states and16 timing cells. These totals cover the fixed continuation window; they do not measure whole-match CPU, game duration, or quality.

Assets were forced through the same high-priority GLOB2_ASSET_DIR and isolated GLOB2_USER_DATA_DIR, with a common working directory. Prior fallback-path observations remain separate and are excluded from these totals.
