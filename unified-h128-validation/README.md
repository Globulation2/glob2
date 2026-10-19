# Unified H128 research qualification

Source pushed to PR1224 at d80e7342c23bf5c7cad0ae3de8fb5fe86f719623. Native production code is identical to0a548c954; later commits update test fixtures, required simulation goldens and Python role/routing setup. This qualifies explicit fixed-interval bootstrap collection, not an untrained adaptive model or stable promotion.

- Mac ordinary release,46unit and86focusedengine cases pass (initial run plus targeted fixture corrections; XML/logs preserve failures and reruns). Linux46unit and94focusedengine cases pass under the same production code.
- Mac/Linux observerOFF/ON,participants1/4 and save continuation: five shared-map runs have identical per-tick world/snapshot/replay checksums and final saves;16diagnosticCSVpairs match. Four Linux hosts also match Mac.
- Version178, replay floor178, SIM84; save floor58 preserved. Required three golden cases regenerated and verified;19MatchSetup/replay gate cases pass.
- Spatial diagnostics support natural Materials and Clear ground fields; weighted Guard/Forbidden border/Market/AIR comparisons explicitly unknown. No new spatial inputs are in production inference.
- H128 capture offsets8..127,128000milli threshold, minimum8, no forced maximum. Cold/reactive/history recovery remain. Generic model is explicitly untrained; only declared positive fixed research intervals may launch.
- Initial6repeat counterbalanced CPU study on3player128map/1024ticks: median CPUms/tick0.0806 probeOFF,2.3283 probeON/spatialOFF,2.4018 probeON/spatialON. Spatial ratio median3.15%; this is diagnostic cost on one small initial workload, not a production-overhead or large/late-game claim. CPU0affinity; SMT sibling16 was not OS-isolated. Full results and limitations preserved.

Build commands, dependency hashes, native inventories, receipts, raw checksums, saves and compressedlogs are attached below. No Windows/browser/mobile or hardware accelerator execution is claimed by this qualification. New model fitting, held-out/large-game results and stable promotion remain pending.
