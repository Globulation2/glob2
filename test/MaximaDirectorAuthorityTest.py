#!/usr/bin/env python3
"""Structural guardrails for the sole-authority Maxima director."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


class MaximaDirectorAuthorityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "src/AIMaxima.h").read_text()
        cls.source = (ROOT / "src/AIMaxima.cpp").read_text()
        cls.farming_policy = (ROOT / "src/AIMaximaFarmingPolicy.cpp").read_text()
        cls.strategy = (ROOT / "src/AIMaximaStrategy.cpp").read_text()
        cls.base = (ROOT / "data/maxima/base.strategy").read_text()

    def test_director_and_immutable_plan_are_explicit_boundaries(self) -> None:
        self.assertIn("class StrategyDirector", self.header)
        self.assertIn("struct DirectorPlan", self.header)
        self.assertIn("StrategyDirector director;", self.header)
        self.assertIn("DirectorPlan budget;", self.header)
        cycle = function(
            self.source,
            "void Maxima::evaluate_strategy",
            "Maxima::Maxima(Player",
        )
        self.assertLess(cycle.index("allocate_resources()"),
                        cycle.index("finalize_director_plan(echo)"))

    def test_classic_authority_and_blending_cannot_return(self) -> None:
        # Legacy save fields remain load-only compatibility state. Restrict the
        # authority check to the active director/evaluator implementation.
        authority = self.source[:self.source.index("Maxima::Maxima(Player")]
        for forbidden in (
            "build_classic_plan", "blend_strategic_plans",
            "blend_plan_value", "dynamic_plan_weight", "classic_plan",
        ):
            self.assertNotIn(forbidden, authority)

    def test_check_phases_is_only_a_director_entrypoint(self) -> None:
        body = function(
            self.source,
            "void Maxima::check_phases",
            "Maxima::collect_building_profiles",
        )
        self.assertEqual(body.count("director.evaluate(*this, echo);"), 1)
        self.assertNotIn("return;", body)
        self.assertNotIn("TeamStat", body)

    def test_schema_v2_contains_no_legacy_groups(self) -> None:
        self.assertIn('"{\\"schemaVersion\\":2', self.strategy)
        keys = re.findall(r'"([a-z0-9_.]+)"\s*,\s*[-0-9]+\s*,', self.strategy)
        for key in keys:
            self.assertFalse(key.startswith(("phases.", "production.", "adaptation.")))
        for legacy in (
            "construction.base_sites", "construction.recovery_extra_sites",
            "military.attack_flag_count", "military.swarm_warrior_ratio",
            "recon.explorer_base", "recon.offense_explorer_min",
            "economy.inn_level1_capacity", "economy.growth_worker_ratio",
        ):
            self.assertNotIn(legacy, self.base)
            self.assertNotIn(f'"{legacy}"', self.strategy)

    def test_selected_executors_consume_only_the_plan(self) -> None:
        ranges = (
            ("void Maxima::manage_inn", "void Maxima::manage_swarm"),
            ("void Maxima::manage_swarm", "void Maxima::OffenseDiagnostics::reset"),
            ("void Maxima::development_cycle", "void Maxima::manage_buildings"),
            ("void Maxima::control_offense", "void Maxima::clear_preemptive_defense"),
            ("void Maxima::update_preemptive_defense", "void Maxima::compute_defense_flag_positioning"),
            ("void Maxima::compute_defense_flag_positioning", "void Maxima::modify_points"),
            ("void Maxima::update_fruit_flags", "void Maxima::update_fruit_alliances"),
        )
        for start, end in ranges:
            executor = function(self.source, start, end)
            self.assertNotIn("strategy.", executor, start)
            if start == "void Maxima::development_cycle":
                self.assertIn("collect_development_intents(world)", executor)
                self.assertIn("collect_development_limits(echo)", executor)
            else:
                self.assertIn("budget.", executor, start)

    def test_dirty_plan_is_recomputed_before_scheduled_executors(self) -> None:
        tick = function(self.source, "void Maxima::tick", "void Maxima::handle_event")
        replan = tick.index("director.evaluate(*this, echo);")
        for executor in ("manage_buildings(echo)", "control_offense(echo)",
                         "update_farming(echo)", "update_fruit_flags(echo)"):
            self.assertLess(replan, tick.index(executor))
        self.assertIn("director.invalidate();", self.source)

    def test_offense_keeps_one_flag_without_muster_or_withdrawal(self) -> None:
        planner = function(self.source, "void Maxima::plan_offense",
                           "void Maxima::end_offense")
        control = function(self.source, "void Maxima::control_offense",
                           "void Maxima::choose_enemy_target")
        tactics = (ROOT / "src/AIMaximaTactics.h").read_text()
        for gone in ("PhaseMuster", "PhaseTransit", "PhaseWithdraw",
                     "PhaseCooldown", "MissionRelief", "musterLaunchAllowed",
                     "casualtiesRequireWithdrawal", "SiegeTargetContinuity"):
            self.assertNotIn(gone, planner)
            self.assertNotIn(gone, control)
            # The retired states are removed from the type, not just unused.
            self.assertNotIn(gone, tactics)
        self.assertIn("ChangeFlagPosition", control)
        self.assertIn("AssignWorkers", control)
        self.assertIn("budget.tactical_requested_force=std::min(cap, surplus);", planner)
        self.assertIn("reason=stalled", control)

    def test_active_siege_selector_honors_quarantine(self) -> None:
        planner = function(self.source, "void Maxima::plan_offense",
                           "void Maxima::end_offense")
        check = planner.index("Tactics::Program::targetQuarantined")
        self.assertIn("strategy.tactics.failed_target_quarantine_enabled",
                      planner[check:check + 220])
        self.assertIn("attack_target_quarantine_until", planner[check:check + 220])

    def test_active_tactical_loop_can_open_a_sealed_enemy_route(self) -> None:
        control = function(self.source, "void Maxima::control_offense",
                           "void Maxima::choose_enemy_target")
        self.assertIn("dig_out_enemy(echo);", control)
        self.assertIn("budget.tactical_dig_out_team>=0", control)
        planner = function(self.source, "void Maxima::plan_offense",
                           "void Maxima::end_offense")
        self.assertIn("strategy.tactics.dig_out_enabled", planner)
        self.assertIn("budget.attack_clearing_workers>0", planner)

    def test_active_mission_force_uses_the_runtime_building_register(self) -> None:
        planner = function(self.source, "void Maxima::plan_offense",
                           "void Maxima::end_offense")
        self.assertIn("get_building(tactical_mission.flagId)", planner)
        # Warriors already enrolled on the live flag stay eligible for it.
        self.assertRegex(planner, r"tactical_warrior_available\([^)]*, flag,")
        eligibility = function(
            self.source, "bool tactical_warrior_available", "int warrior_power"
        )
        self.assertIn("warrior->attachedBuilding==continuingFlag", eligibility)

    def test_preemptive_defense_remains_active_under_pressure(self) -> None:
        update = function(
            self.source,
            "void Maxima::update_preemptive_defense",
            "void Maxima::compute_defense_flag_positioning",
        )
        self.assertNotIn("food_emergency", update)
        self.assertNotIn("colony_emergency", update)
        self.assertIn("if(!budget.preemptive_defense_active)", update)
        self.assertIn("clear_preemptive_defense(echo);", update)
        self.assertIn("budget.preemptive_effective_zone_max", update)
        self.assertIn("budget.preemptive_amphibious_active", update)
        self.assertIn("topologyRefreshRequired", update)

        # Differential ownership preserves unrelated pre-existing guard areas.
        self.assertIn("preemptive_guard_tiles.find(*tile)", update)
        self.assertIn("if(!map.is_guard_area(*tile%w, *tile/w))", update)
        self.assertNotIn("preemptive_diagnostics", function(
            self.source,
            "void Maxima::saveDirector",
            "bool Maxima::loadDirector",
        ))
