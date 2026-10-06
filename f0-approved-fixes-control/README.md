# Fresh f0 approved-fix control tournament

This campaign isolates the three approved fixes against pure master f0ff8384b7e47d5867ff630ef8f3387591de3baa. It is not the resource-refactor candidate comparison and contains no valid performance measurements.

Pure executable SHA256: `842ca2aecc71c2cb48f9a26b146cd9c39dec3cab214deb260191719dc1592c00`. Approved-fix executable SHA256: `eb48402f6332a0f519b7f6b6e6f908adc359f0c69ed5acc301efabffe3e46dc9`. Exact seven-file patch and source/build/runtime inventories are retained.

84 successful runs form 42 paired fixtures: 40 primary games over eight terrain/team-count/map-seed geometry clusters, including mixed-AI swapped sides, plus two historical64x64 controls reported separately. Map seeds1001/1002, game seed19, cap90000ticks, jobs4. Generation commands and frozen save hashes accompany every fixture. Original campaign and analysis exited0, without recovery.

Intervals below bootstrap geometry-cluster means with10000 resamples and seed4242. They are descriptive: two seed values/eight geometries do not establish equivalence, and multiple metrics are not adjusted for multiplicity. All metrics and subgroups are included in all-metrics.csv and summary.json.

|Metric|Pure mean|Approved mean|Geometry-balanced change|95% clustered interval|
|---|---:|---:|---:|---|
|resolved_outcome|0.35|0.375|0.025|[-0.15000000000000002, 0.2]|
|observed_winner_set_change|0|0.3333|0.3333|[0, 0.7333333333333333]|
|resolution_status_change|0|0.275|0.275|[0.15000000000000002, 0.4]|
|units|70.28|58.25|-12.03|[-27.5, -1.8]|
|starvation_deaths|68.65|75.97|7.325|[-5.25, 21.175]|
|new_buildings|39.2|34.67|-4.525|[-8.6, -1.425]|
|harvested_food_per_1000_ticks|18.54|16.69|-1.85|[-4.074846155012549, -0.05956078500848218]|
|delivered_food_per_1000_ticks|17.98|16.27|-1.706|[-3.6879090347851524, -0.0531215588617385]|
|harvested_wood_per_1000_ticks|3.373|2.823|-0.5502|[-1.4177287466207782, 0.052511800305378484]|
|delivered_wood_per_1000_ticks|3.144|2.648|-0.4956|[-1.2645181028336532, 0.03917324641446637]|
|new_buildings_1_observed_fraction|1|1|0|[0.0, 0.0]|
|new_buildings_1_waiting_lower_ticks|1578|1558|-19.2|[-57.6, 25.6]|
|new_buildings_1_waiting_upper_ticks|2090|2070|-19.2|[-57.6, 25.6]|

Winners changed in3 of9 jointly resolved primary pairs. Unresolved/tick-cap games are censored, not losses; resolved-only win shares are subject to selection bias.

Construction milestones are interval-observed at512tick cadence; upper/lower waiting bounds retain right censoring and game-end/elimination competing events. These intervals do not include within-sample or competing-event uncertainty. No individual project-start timestamps exist. Harvest units are raw packets; deliveries/consumption use building stock units and are not normalized across recipe multipliers. Final counts depend on game duration; throughput rates are reported separately.

All248 team observations have complete final telemetry; parser errors0. Each .tar.zst contains unmodified raw logs under repository-relative paths. inventory.json provides compressed and raw-member SHA256 identities; every archive member was decompressed and matched to its original run hash before publication. The compact ZIP contains exact commands, runtime input bytes, fixture bytes, provenance, all metrics and per-team telemetry. Binaries/dependency caches/full source are excluded; source commits and patch identify them.
