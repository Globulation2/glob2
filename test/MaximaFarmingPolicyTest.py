#!/usr/bin/env python3
"""Structural checks for Maxima-only farming policy and save compatibility."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MaximaFarmingPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "src/AIMaxima.h").read_text()
        cls.core = (ROOT / "src/AIMaxima.cpp").read_text()
        cls.policy = (ROOT / "src/AIMaximaFarmingPolicy.cpp").read_text()
        cls.source = cls.policy + "\n" + cls.core
        cls.primitive = (ROOT / "src/AIMaximaFarming.cpp").read_text()
        cls.placement = (ROOT / "src/AIMaximaPlacement.cpp").read_text()
        cls.base_strategy = (ROOT / "data/maxima/base.strategy").read_text()

    def test_farming_and_clearing_are_extracted_as_one_policy_unit(self):
        methods = (
            "record_construction_space_failure",
            "manage_land_clearing",
            "update_maintenance_clearing_areas",
            "initialize_farming_cache",
            "available_expansion_neighbors",
            "update_farming",
        )
        for method in methods:
            definition = f"Maxima::{method}"
            self.assertIn(definition, self.policy)
            self.assertNotIn(definition, self.core)
        self.assertIn(
            "AIMaximaFarmingPolicy.cpp",
            (ROOT / "src/SConscript").read_text(),
        )

    def test_cache_is_private_and_not_serialized(self):
        self.assertIn("Farming::ExactFertilityCache fertility_cache", self.header)
        director = self.source[self.source.index("void Maxima::saveDirector"):]
        director = director[:director.index("bool Maxima::loadDirector")]
        self.assertNotIn("fertility_cache", director)

    def test_proactive_wood_clearing_never_targets_wheat(self):
        conversion = self.source[self.source.index("struct Maxima::WoodClearingTarget"): ]
        conversion = conversion[:conversion.index("void Maxima::update_maintenance_clearing_areas")]
        self.assertIn("clearing_resources[WOOD]=true", conversion)
        self.assertNotIn("clearing_resources[CORN]=true", conversion)
        self.assertIn("IntBuildingType::CLEARING_FLAG, 0", conversion)
        self.assertIn("AssignWorkers(\n\t\t\t\tstrategy.staffing.clearing_workers", conversion)
        self.assertIn("if(wood<2)", conversion)

    def test_proactive_clearing_has_live_pressure_inputs(self):
        self.assertIn(
            "opening_space_constrained=Farming::openingSpaceConstrained(",
            self.source,
        )
        self.assertIn("record_construction_space_failure();", self.source)
        self.assertIn(
            "diagnostics.rejected[RejectedClearableResource]", self.source
        )

    def test_placement_and_gate_contracts_create_owned_clearing_areas(self):
        maintenance = self.source[
            self.source.index(
                "Maxima::MaintenanceClearingPlan Maxima::build_maintenance_clearing_plan"
            ):
        ]
        maintenance = maintenance[:maintenance.index("void Maxima::initialize_farming_cache")]
        self.assertIn("AddArea(ClearingArea)", maintenance)
        self.assertIn("RemoveArea(ClearingArea)", maintenance)
        self.assertIn("contract.footprintTiles", maintenance)
        self.assertIn("contract.circulationTiles", maintenance)
        self.assertIn("selectResourcePreservingCirculation", maintenance)
        self.assertIn("cell.resource.type==CORN", maintenance)
        self.assertIn("cell.resource.type==WOOD", maintenance)
        self.assertIn("!applied_maintenance_clearing_mask[index]", maintenance)
        self.assertIn('"\\treservation_resources_preserved="', maintenance)
        self.assertIn('"\\treservation_fallback_entrances="', maintenance)
        self.assertIn("applied_maintenance_clearing_mask[index]", maintenance)
        self.assertIn("building_footprint=map->getTile(x, y).building!=NOGBID", maintenance)
        self.assertIn("const bool desired=!building_footprint", maintenance)
        self.assertNotIn("RuntimeEvent::UpdateClearing", self.source)
        self.assertNotIn("Planner::clearingTiles", self.placement)

    def test_maintenance_overgrowth_drives_renewable_clearing(self):
        clearing = self.source[self.source.index("void Maxima::manage_land_clearing"):]
        clearing = clearing[:clearing.index("void Maxima::update_maintenance_clearing_areas")]
        self.assertIn("maintenance_wood_tiles>0", clearing)
        self.assertIn("best_maintenance_wood>0", clearing)
        self.assertIn('"maintenance_overgrowth"', clearing)
        self.assertNotIn("stat->needHeal", clearing)
        self.assertNotIn("proactive_campaign_cap", self.source)
        self.assertNotIn("proactive_campaign_cap", self.base_strategy)

    def test_wood_adjacent_to_protected_wheat_is_maintenance_work(self):
        farming = self.policy[self.policy.index(
            "Maxima::FarmProtectionPlan Maxima::build_farming_protection_plan"
        ):]
        maintenance = self.policy[
            self.policy.index(
                "Maxima::MaintenanceClearingPlan Maxima::build_maintenance_clearing_plan"
            ):
        ]
        maintenance = maintenance[:maintenance.index("void Maxima::initialize_farming_cache")]
        self.assertIn("cell.resource.type==WOOD", maintenance)
        self.assertIn("wheat_invasion_clearing_required", maintenance)
        self.assertIn("plan.circulation[index]=1", maintenance)
        self.assertIn('"\\twheat_invasion_wood="', maintenance)

    def test_firebreak_is_fertility_banded_and_yields_to_farms(self):
        maintenance = self.source[
            self.source.index(
                "Maxima::MaintenanceClearingPlan Maxima::build_maintenance_clearing_plan"
            ):
        ]
        maintenance = maintenance[:maintenance.index("void Maxima::initialize_farming_cache")]
        self.assertIn("Farming::fertilityWithinPercentBand", maintenance)
        self.assertIn("plan.firebreak[index]=wants_firebreak", maintenance)
        self.assertIn("&& cell.resource.type==WOOD", maintenance)
        self.assertIn("if(contract_desired && map->isForbidden", maintenance)
        self.assertIn("farming.wood_firebreak_fertility_min_percent = 5", self.base_strategy)
        self.assertIn("farming.wood_firebreak_fertility_max_percent = 14", self.base_strategy)

    def test_wheat_and_wood_share_preemptive_farm_pattern(self):
        # Native tests cover parity, moving boundaries, wrapping and orders.
        self.assertIn("wheat_role=classify_farm_tile", self.policy)
        self.assertIn("wood_role=classify_farm_tile", self.policy)
        self.assertIn("Farming::isInteriorSeed", self.policy)
        self.assertIn("Farming::isExpansionCell", self.policy)

    def test_wood_firebreak_remains_additional_override(self):
        farming = self.policy[self.policy.index(
            "Maxima::FarmProtectionPlan Maxima::build_farming_protection_plan"
        ):]
        farming = farming[:farming.index("void Maxima::apply_farming_protection")]
        self.assertLess(farming.index("else if(wheat_farm)"), farming.index("wood_firebreak_mask[index]"))
        self.assertLess(farming.index("wood_firebreak_mask[index]"), farming.index("else if(wood_farm)"))

    def test_sparse_wheat_lattice_leaves_complete_access_lanes(self):
        reserved = {(x, y) for y in range(8) for x in range(8) if (x & 1) and (y & 1)}
        for coordinate in range(0, 8, 2):
            self.assertTrue(all((x, coordinate) not in reserved for x in range(8)))
            self.assertTrue(all((coordinate, y) not in reserved for y in range(8)))

    def test_farm_shoreline_includes_partial_sand_tiles(self):
        farming = self.policy[self.policy.index("struct FarmTileClassification"):]
        farming = farming[:farming.index(
            "struct Maxima::FarmProtectionPlan"
        )]
        self.assertIn("bool shoreline_backed", farming)
        helper = self.policy[self.policy.index("std::vector<Uint8> shoreline_backing"):]
        helper = helper[:helper.index("bool is_empty_growth_cell")]
        self.assertIn("map->hasSand(nx, ny)", helper)
        self.assertIn("map->isWater(x, y)", helper)
        self.assertNotIn("ressource", helper)
        self.assertNotIn("mi.is_sand(x+dx, y+dy)", farming)

    def test_shoreline_connectivity_is_cached_once_per_map_load(self):
        initializer = self.policy[self.policy.index("void Maxima::initialize_farming_cache"):]
        initializer = initializer[:initializer.index("int Maxima::available_expansion_neighbors")]
        self.assertEqual(self.policy.count("shoreline_backing(map)"), 1)
        self.assertIn("shoreline_backing(map)", initializer)
        self.assertIn("farming_shoreline_mask.size()==size_t(w*h)", initializer)
        self.assertIn("farming_cardinal_shoreline_mask.size()==size_t(w*h)", initializer)
        self.assertIn("farming_shoreline_mask[index]!=0", self.policy)

    def test_wheat_wood_boundary_conversion_is_removed(self):
        self.assertNotIn("select_barrier_conversion_target", self.header)
        self.assertNotIn("select_barrier_conversion_target", self.source)
        self.assertNotIn("reason=barrier_conversion", self.source)
        self.assertIn("if(wood<2)", self.source)

    def test_default_fertility_cutoffs(self):
        self.assertRegex(
            self.base_strategy,
            r"(?m)^farming\.wheat_fertility_min = 3276$",
        )
        self.assertRegex(
            self.base_strategy,
            r"(?m)^farming\.wood_fertility_base_percent = 15$",
        )
        self.assertRegex(
            self.base_strategy,
            r"(?m)^farming\.wood_fertility_pressure_percent = 10$",
        )

    def test_subbehaviors_are_independently_optimizable(self):
        keys = (
            "farming.resource_preserving_circulation_enabled",
            "farming.wheat_invasion_clearing_enabled",
            "farming.wood_firebreak_enabled",
        )
        for key in keys:
            self.assertRegex(
                self.base_strategy,
                rf"(?m)^{re.escape(key)} = true$",
            )
            self.assertIn(key, self.source)
        self.assertIn("budget.farming_resource_preserving_circulation_enabled", self.policy)
        self.assertIn("budget.farming_wheat_invasion_clearing_enabled", self.policy)
        self.assertIn("budget.farming_wood_firebreak_enabled", self.policy)

    def test_exact_integer_kernel_has_no_float_or_sqrt(self):
        self.assertNotIn("sqrt", self.primitive)
        self.assertNotIn("float", self.primitive)
        self.assertIn("OFFSET_WEIGHT", self.primitive)

if __name__ == "__main__":
    unittest.main()
