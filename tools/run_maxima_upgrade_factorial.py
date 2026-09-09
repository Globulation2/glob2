#!/usr/bin/env python3
"""Test Maxima's early builder pipeline and construction throughput separately."""

from __future__ import annotations

import run_maxima_staffing_ablation as runner

runner.__doc__ = __doc__


# Every variant explicitly pins the swarm settings that were promoted locally.
# Remote tournament roots may predate that promotion, so this keeps the causal
# comparison identical on every host without copying a dirty working tree.
runner.BASE_PARAMETERS = {
    "economy.committed_swarm_workers": 14,
    "economy.swarm_workers_per_building_tight": 5,
    "economy.swarm_workers_per_building_healthy": 6,
    "economy.swarm_workers_per_building_surplus": 8,
}

# Move the first school to the existing level-one upgrade population threshold.
# The school construction crew becomes its training crew on completion, so this
# directly tests whether Maxima's BUILD-skill pipeline is simply starting late.
EARLY_SCHOOL = {
    "economy.school_population_min": 30,
    "economy.school_bid_utility_min": 10,
}

# Give the opening production and training sites 50% more labor without raising
# the number of simultaneous sites. This isolates initial-build throughput from
# the broad construction bundle that was already shown to be neutral/harmful.
INITIAL_STAFFING = {
    "staffing.construction_inn_workers": 3,
    "staffing.construction_swarm_workers": 3,
    "staffing.construction_training_workers": 6,
}

# Reduce the trained-builder population needed to authorize parallel upgrades.
# Per-upgrade staffing (8/12 workers) and all upgrade timing gates stay fixed.
UPGRADE_PARALLELISM = {
    "upgrades.level1_trained_units_per_slot": 8,
    "upgrades.level2_trained_units_per_slot": 12,
}

runner.FACTORS = (
    ("early_school", EARLY_SCHOOL),
    ("initial_staffing", INITIAL_STAFFING),
    ("upgrade_parallelism", UPGRADE_PARALLELISM),
)
runner.FIELDS = runner.FIELDS + (
    "build_upgraded_workers", "level1_buildings", "level2_buildings",
    "level3_buildings", "food", "food_capacity", "critical_food",
    "unserved_food", "attack_upgraded_warriors", "swim_upgraded_warriors",
)
runner.EXPERIMENT_TITLE = "Maxima builder-pipeline factorial ablation"
runner.EXPERIMENT_DESCRIPTION = (
    "The matched-seed 2x2x2 design separates earlier BUILD training, extra "
    "labor on initial production/training sites, and greater building-upgrade "
    "parallelism. The promoted swarm staffing is pinned in every variant."
)
runner.OUTPUT_STEM = "maxima-upgrade-factorial"


if __name__ == "__main__":
    raise SystemExit(runner.main())
