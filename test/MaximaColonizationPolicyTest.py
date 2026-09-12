#!/usr/bin/env python3
"""Structural and configuration guardrails for Maxima colonization."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MaximaColonizationPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.maxima = (ROOT / "src/AIMaxima.cpp").read_text()
        cls.header = (ROOT / "src/AIMaxima.h").read_text()
        cls.placement = (ROOT / "src/AIMaximaPlacement.cpp").read_text()
        cls.placement_header = (ROOT / "src/AIMaximaPlacement.h").read_text()
        cls.base = (ROOT / "data/maxima/base.strategy").read_text()

    def test_schema_v2_colonization_defaults_are_complete(self) -> None:
        expected = {
            "enabled": "true", "minimum_anchor_distance": "24",
            "maximum_threat": "35", "minimum_new_food": "8",
            "minimum_value": "5", "conquered_merge_radius": "10",
            "conquered_memory_ticks": "12000",
        }
        values = dict(re.findall(
            r"^colonization\.([a-z_]+)\s*=\s*(\S+)", self.base, re.M
        ))
        self.assertEqual(expected, values)
        packaged = (ROOT / "Glob2.app/Contents/Resources/data/maxima/base.strategy")
        self.assertEqual(self.base, packaged.read_text())

    def test_director_uses_labor_and_startup_instead_of_phase_gates(self) -> None:
        start = self.maxima.index("const bool colony_active=")
        end = self.maxima.index("budget.can_swim=", start)
        policy = self.maxima[start:end]
        for text in ("snapshot.free_workers-snapshot.worker_jobs_open",
                     "construction_swarm_workers", "operating_colonies",
                     "unitsWorking", "resourceForOneUnit"):
            self.assertIn(text, policy)
        for text in ("food_headroom", "recovery_active", "food_emergency",
                     "cooldown_ticks", "population_min"):
            self.assertNotIn(text, policy)

    def test_independent_colony_intent_and_food_claim_contract(self) -> None:
        start = self.maxima.index("Maxima::collect_development_intents")
        end = self.maxima.index("Maxima::collect_development_limits", start)
        intents = self.maxima[start:end]
        self.assertIn("colony.purpose=ColonySeed", intents)
        self.assertIn("colony.unmetCount=1", intents)
        for text in ("prepareColonyClaims(world,action.id)", "colonyFoodClaims[index]",
                     "colonyMinimumFood", "colonyMinimumValue"):
            self.assertIn(text, self.placement)

    def test_cleared_sites_are_remembered_merged_decayed_and_expired(self) -> None:
        self.assertIn("remember_cleared_enemy_site(echo, sighting->second)", self.maxima)
        self.assertLess(
            self.maxima.index("remember_cleared_enemy_site(echo, sighting->second)"),
            self.maxima.index("reconnaissance.confirmBuildingAbsent(*team, *gid)"),
        )
        for behavior in (
            "conquered_merge_radius",
            "conquered_memory_ticks",
            "cleared_enemy_sites.erase(site)",
        ):
            self.assertIn(behavior, self.maxima)

    def test_v92_persists_state_and_old_actions_default_to_core(self) -> None:
        version = (ROOT / "src/Version.h").read_text()
        self.assertRegex(version, r"#define VERSION_MINOR (?:9[2-9]|[1-9][0-9]{2,})")
        # Only decision state is durable. The colony completion tick and the
        # established-colony counter are diagnostic only: nothing reads them
        # back, so restoring them would not change a single order.
        for durable in (
            'writeEnterSection("ColonizationState")',
            '"last_accounted_action_id"',
            '"hotspot_count"',
        ):
            self.assertIn(durable, self.maxima)
        self.assertIn("development_planner.load(stream, versionMinor)", self.maxima)
        self.assertIn("versionMinor>=92?static_cast<DevelopmentPurpose>",
                      self.placement)
        self.assertIn(":CoreCapacity", self.placement)

    def test_diagnostics_and_explicit_events_cover_colony_lifecycle(self) -> None:
        for field in (
            "purpose=", "colonization_eligible=", "colonization_gate=",
            "active_colonial_action=",
            "friendly_distance=", "corn_distance=", "colony_new_food=",
            "colony_value=", "hotspot_count=",
        ):
            self.assertIn(field, self.maxima)
        for event in (
            '"colony_site_remembered"',
            '"colony_swarm_selected"',
            '"colony_swarm_completed"',
            '"colony_swarm_failed"',
        ):
            self.assertIn(event, self.maxima)


if __name__ == "__main__":
    unittest.main()
