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
        cls.optimizer = (ROOT / "tools/optimize_maxima.py").read_text()

    def test_farming_and_clearing_are_extracted_as_one_policy_unit(self):
        methods = (
            "record_construction_space_failure",
            "manage_land_clearing",
            "update_maintenance_clearing_areas",
            "initialize_farming_cache",
            "compute_farming_topology_signature",
            "update_barrier_topology",
            "barrier_gate_is_clear",
            "barrier_gate_is_resource_clearable",
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

    def test_gate_invariants_and_empty_barrier_policy(self):
        self.assertIn("STRATEGIC_GATE_COUNT=2", self.source)
        self.assertIn("STRATEGIC_GATE_WIDTH=3", self.source)
        self.assertNotIn("STRATEGIC_GATE_INLAND_DEPTH", self.source)
        self.assertIn(
            "&& !strategic_barrier) continue;",
            self.source,
        )
        self.assertIn("strategic_gate_mask[index] || emergency_escape_mask[index]", self.source)

    def test_gate_pairs_belong_to_settlements_and_channels_reach_home(self):
        # Native real-map tests verify connectivity. These checks protect the
        # architectural boundary: local pairs, complete routes and stable caches.
        topology = self.policy[self.policy.index("void Maxima::update_barrier_topology"):]
        topology = topology[:topology.index("bool Maxima::barrier_gate_is_clear")]
        self.assertIn("settlement_weight", topology)
        self.assertIn("local_home_sources", topology)
        self.assertIn("++accepted_components", topology)
        self.assertIn("const std::vector<int> selected[STRATEGIC_GATE_COUNT]", topology)
        self.assertIn("geometry==barrier_geometry_signature", topology)
        self.assertIn("candidate.channel=build_gate_channel", topology)
        helper = self.policy[self.policy.index("GateChannel build_gate_channel"):]
        helper = helper[:helper.index("int gate_resource_burden")]
        self.assertIn("for(int offset=-1; offset<=1; ++offset)", helper)
        self.assertIn("home_distance[next]<home_distance[best]", helper)

    def test_access_and_defense_consume_the_same_reserved_routes(self):
        repair = self.policy[self.policy.index("int Maxima::connect_settlements_to_gates"):]
        repair = repair[:repair.index("void Maxima::resolve_wheat_invasion_clearing")]
        self.assertIn("settlement_access_routes", repair)
        self.assertIn("outside[i]&&emergency_escape_mask[i]", repair)
        self.assertIn("farming_shoreline_mask[next]", repair)
        self.assertIn("tile.gateCorridor", self.placement)
        self.assertIn("towerGateWeight", self.placement)
        self.assertIn("refresh_gate_defense", self.policy)

    def test_gate_selection_prefers_open_gate_and_channel_tiles(self):
        topology = self.source[self.source.index("void Maxima::update_barrier_topology"):]
        topology = topology[:topology.index("bool Maxima::barrier_gate_is_clear")]
        self.assertIn("StrategicGateOption", self.policy)
        self.assertIn("candidate.resource_burden=gate_resource_burden", topology)
        self.assertIn("build_gate_channel(candidate.tiles", topology)
        self.assertIn("cell.ressource.type!=NO_RES_TYPE", self.policy)
        self.assertIn("pair_burden=gate_options[first].resource_burden", topology)
        self.assertIn("pair_score<best_pair_score", topology)
        self.assertIn("pair_score==best_pair_score", topology)
        self.assertLess(
            topology.index("pair_score<best_pair_score"),
            topology.index("separation>best_separation"),
        )
        self.assertIn('"\\tgate_resource_burden="', topology)

    def test_gate_selection_prefers_stable_nearby_positions(self):
        topology = self.source[self.source.index("void Maxima::update_barrier_topology"):]
        topology = topology[:topology.index("bool Maxima::barrier_gate_is_clear")]
        self.assertIn("previous_gates=strategic_gates", topology)
        self.assertIn("Farming::gateRelocationDistance", topology)
        self.assertIn("Farming::gatePairRelocationDistance", topology)
        self.assertIn("gate_options[first].tiles", topology)
        self.assertIn("budget.farming_gate_relocation_penalty_cap", topology)
        self.assertIn("pair_score=pair_burden+relocation_penalty", topology)
        self.assertIn("relocation_distance<best_relocation_distance", topology)
        self.assertIn("farming.gate_relocation_penalty_cap = 4", self.base_strategy)

    def test_cache_is_private_and_not_serialized(self):
        self.assertIn("Farming::ExactFertilityCache fertility_cache", self.header)
        director = self.source[self.source.index("void Maxima::saveDirector"):]
        director = director[:director.index("bool Maxima::loadDirector")]
        self.assertNotIn("fertility_cache", director)
        self.assertNotIn("strategic_barrier_mask", director)

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

    def test_gate_clearing_is_economy_gated_except_when_boxed_in(self):
        clearing = self.source[self.source.index(
            "Maxima::GateClearingIntent Maxima::select_gate_clearing_intent"
        ):]
        clearing = clearing[:clearing.index("void Maxima::update_maintenance_clearing_areas")]
        self.assertIn("budget.farming_gate_clearing_radius", clearing)
        self.assertIn("inspect_gate_route", clearing)
        self.assertIn("if(first_blocked && second_blocked", clearing)
        self.assertIn("intent.emergency=true", clearing)
        self.assertIn("costly_economy_ready", clearing)
        self.assertIn("wood_economy_ready", clearing)
        self.assertIn("strategy.farming.gate_clearing_workers_min", clearing)
        self.assertIn("snapshot.hospitals>0", clearing)
        self.assertIn("environment.food_headroom>=strategy.economy.mature_food_headroom_min", clearing)
        self.assertIn("gate_target=gate_intent.target", clearing)
        self.assertLess(
            clearing.index("if(first_blocked && second_blocked"),
            clearing.index("const bool first_ready="),
        )
        self.assertNotIn("farming_allow_gate_clearing", clearing)
        self.assertNotIn("farming_gate_clearing_duration", clearing)
        self.assertNotIn("ChangeFlagSize(4,\n\t\t\t\t\tproactive_clearing_flag)", clearing)
        self.assertIn("farming.gate_clearing_radius = 1", self.base_strategy)
        self.assertIn("farming.gate_clearing_workers_min = 50", self.base_strategy)
        for removed_setting in (
            "gate_clearing_start_tick",
            "gate_clearing_population_min",
            "gate_clearing_spare_workers_min",
            "gate_clearing_hospitals_min",
            "gate_clearing_food_headroom_min",
            "gate_clearing_resource_capacity_min",
            "gate_clearing_duration_ticks",
        ):
            self.assertNotIn(removed_setting, self.base_strategy)
        self.assertNotIn("gateClearingEconomyReady", self.primitive)

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
        self.assertIn("cell.ressource.type==CORN", maintenance)
        self.assertIn("cell.ressource.type==WOOD", maintenance)
        self.assertIn("!applied_maintenance_clearing_mask[index]", maintenance)
        self.assertIn('"\\treservation_resources_preserved="', maintenance)
        self.assertIn('"\\treservation_fallback_entrances="', maintenance)
        self.assertIn("strategic_gate_mask[index]", maintenance)
        self.assertIn("emergency_escape_mask[index]", maintenance)
        self.assertIn("applied_maintenance_clearing_mask[index]", maintenance)
        self.assertIn("building_footprint=map->getCase(x, y).building!=NOGBID", maintenance)
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
        self.assertIn(
            "&& (wheat_farm || (strategic_barrier && wheat))", farming
        )
        self.assertIn("cell.ressource.type==WOOD", maintenance)
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
        self.assertIn("&& cell.ressource.type==WOOD", maintenance)
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
        farming = farming[:farming.index("void Maxima::restore_passive_coastal_access")]
        self.assertLess(farming.index("else if(wheat_farm)"), farming.index("wood_firebreak_mask[index]"))
        self.assertLess(farming.index("wood_firebreak_mask[index]"), farming.index("else if(wood_farm)"))

    def test_ambiguous_diagonal_edge_is_not_promoted_to_hard_outer_edge(self):
        def is_outer(shoreline_backed, toward_water, toward_land):
            return shoreline_backed or (toward_water and not toward_land)

        self.assertFalse(is_outer(False, True, True))
        self.assertTrue(is_outer(True, True, True))
        self.assertTrue(is_outer(False, True, False))
        self.assertFalse(is_outer(False, False, True))

    def test_sparse_wheat_lattice_leaves_complete_access_lanes(self):
        reserved = {(x, y) for y in range(8) for x in range(8) if (x & 1) and (y & 1)}
        for coordinate in range(0, 8, 2):
            self.assertTrue(all((x, coordinate) not in reserved for x in range(8)))
            self.assertTrue(all((coordinate, y) not in reserved for y in range(8)))

    def test_only_strategic_barrier_components_become_porous(self):
        farming = self.policy[self.policy.index("void Maxima::restore_passive_coastal_access"):]
        self.assertNotIn("porosity_envelope_mask(farm_protection_mask)", farming)
        self.assertIn("plan.coastal_envelope", farming)
        self.assertIn("makeSealedCoastalFarmBarriersPorous", farming)
        self.assertIn("plan.coastal_envelope, coastal_openings", farming)
        self.assertIn("home_growing_region", farming)
        self.assertIn("coastal_openings[index]=0", farming)
        self.assertIn("originally_forbidden[index] && !plan.forbidden[index]", farming)
        self.assertIn('"\\tporous_infrastructure_overlap="', farming)
        repair = farming[
            farming.index("if(originally_forbidden[index] && !plan.forbidden[index])"):
            farming.index("void Maxima::apply_farming_protection")
        ]
        self.assertNotIn("strategic_gate_mask[index]=1", repair)
        topology = self.policy[
            self.policy.index("void Maxima::update_barrier_topology"):
            self.policy.index("bool Maxima::barrier_gate_is_clear")
        ]
        self.assertNotIn("porosity_x", topology)
        self.assertIn("strategic_gate_mask[gate_tile]=1", topology)
        self.assertIn("strategic_barrier_mask[gate_tile]=0", topology)
        self.assertIn('"\\tporous_components="', farming)
        self.assertIn('"\\tporous_routes="', farming)
        self.assertIn('"\\tporous_tiles="', farming)

    def test_barrier_topology_rejects_permanent_resources(self):
        topology = self.source[self.source.index("Uint32 Maxima::compute_farming_topology_signature"):]
        topology = topology[:topology.index("bool Maxima::barrier_gate_is_clear")]
        self.assertGreaterEqual(topology.count("permanent_resource"), 4)
        self.assertIn("&& !permanent_resource", topology)
        self.assertIn("cell.ressource.type)+1u", topology)

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
        initializer = initializer[:initializer.index("Uint32 Maxima::compute_farming_topology_signature")]
        self.assertEqual(self.policy.count("shoreline_backing(map)"), 1)
        self.assertIn("shoreline_backing(map)", initializer)
        self.assertIn("farming_shoreline_mask.size()==size_t(w*h)", initializer)
        self.assertIn("farming_cardinal_shoreline_mask.size()==size_t(w*h)", initializer)
        self.assertIn("farming_shoreline_mask[index]!=0", self.policy)
        self.assertIn("shore[index]=farming_cardinal_shoreline_mask[index]", self.policy)

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
            "farming.coastal_porosity_enabled",
            "farming.resource_preserving_circulation_enabled",
            "farming.wheat_invasion_clearing_enabled",
            "farming.wood_firebreak_enabled",
        )
        for key in keys:
            self.assertIn(f'"{key}"', self.optimizer)
            self.assertRegex(
                self.base_strategy,
                rf"(?m)^{re.escape(key)} = true$",
            )
            self.assertIn(key, self.source)
        self.assertIn("if(!budget.farming_coastal_porosity_enabled) return", self.policy)
        self.assertIn("budget.farming_resource_preserving_circulation_enabled", self.policy)
        self.assertIn("budget.farming_wheat_invasion_clearing_enabled", self.policy)
        self.assertIn("budget.farming_wood_firebreak_enabled", self.policy)

    def test_exact_integer_kernel_has_no_float_or_sqrt(self):
        self.assertNotIn("sqrt", self.primitive)
        self.assertNotIn("float", self.primitive)
        self.assertIn("OFFSET_WEIGHT", self.primitive)


if __name__ == "__main__":
    unittest.main()
