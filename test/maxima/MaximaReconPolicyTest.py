#!/usr/bin/env python3
"""Structural acceptance checks for Maxima reconnaissance integration."""

from pathlib import Path
import re
import unittest

from source_contracts import function


ROOT = Path(__file__).resolve().parents[2]


class MaximaReconPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "src/ai/maxima/AIMaxima.h").read_text()
        cls.source = (ROOT / "src/ai/maxima/AIMaxima.cpp").read_text()
        cls.world_helpers = (ROOT / "src/ai/maxima/AIMaximaWorldHelpers.h").read_text()
        cls.state = (ROOT / "src/ai/maxima/AIMaximaState.cpp").read_text()
        cls.recon = (ROOT / "src/ai/maxima/AIMaximaRecon.cpp").read_text()
        cls.runtime = (ROOT / "src/ai/maxima/AIMaximaRuntime.h").read_text()

    def test_director_refreshes_recon_before_snapshot(self):
        cycle = self.source[self.source.index("void Maxima::evaluate_strategy"):]
        self.assertLess(cycle.index("update_reconnaissance(echo)"),
                        cycle.index("collect_snapshot(echo)"))
        snapshot = self.source[self.source.index("Maxima::StrategicSnapshot Maxima::collect_snapshot"):]
        snapshot = snapshot[:snapshot.index("void Maxima::update_trends")]
        self.assertIn("reconnaissance.report()", snapshot)

    def test_director_returns_objectives_to_recon_executor(self):
        cycle = self.source[self.source.index("void Maxima::evaluate_strategy"):]
        cycle = cycle[:cycle.index("Maxima::Maxima(Player")]
        self.assertIn("plan_reconnaissance_objectives(echo)", cycle)
        self.assertNotIn("update_reconnaissance_missions(echo)", cycle)
        executor = self.source[self.source.index("void Maxima::update_reconnaissance_missions"):]
        executor = executor[:executor.index("void Maxima::plan_reconnaissance_objectives")]
        self.assertIn("budget.reconnaissance_objectives", executor)
        self.assertNotIn("Recon::Program::planObjectives", executor)

    def test_live_observations_are_fow_gated(self):
        update = self.source[self.source.index("void Maxima::update_reconnaissance"):]
        update = update[:update.index("void Maxima::update_opponent_models")]
        self.assertIn("isFOWDiscovered", update)
        self.assertIn("remembered_footprint_currently_visible", update)
        self.assertIn("confirmBuildingAbsent", update)
        visibility = function(self.world_helpers, "inline bool building_currently_visible")
        self.assertIn("isFOWDiscovered", visibility)
        self.assertIn("player->team->me", visibility)
        self.assertIn("building->type->width", visibility)
        self.assertIn("building->type->height", visibility)

    def test_lightweight_force_sampling_is_phased_and_unit_only(self):
        sampler = self.source[
            self.source.index("void Maxima::sample_reconnaissance_forces") :
            self.source.index("void Maxima::update_reconnaissance")
        ]
        self.assertIn("beginForceObservation", sampler)
        self.assertIn("finishForceObservation", sampler)
        self.assertIn("isFOWDiscovered", sampler)
        self.assertIn("replaceThreats", sampler)
        self.assertNotIn("observeBuilding", sampler)
        self.assertNotIn("observeEconomicActivity", sampler)
        tick = self.source[self.source.index("void Maxima::tick") :]
        self.assertIn("force_sample_interval_ticks", tick)
        self.assertIn("force_sample_phase_offset_ticks", tick)

    def test_recon_state_is_saved(self):
        version = (ROOT / "src/Version.h").read_text()
        minor = int(re.search(r"#define VERSION_MINOR (\d+)", version).group(1))
        self.assertGreaterEqual(minor, 115)
        loader = self.state[self.state.index("bool Maxima::loadDirector"): ]
        loader = loader[:loader.index("bool Maxima::load(")]
        self.assertIn("stream->readEnterSection(\"Recon\")", loader)
        self.assertIn("reconnaissance.reset()", loader)

    def test_flag_event_is_appended_and_owned_separately(self):
        enum_body = self.runtime[self.runtime.index("enum Type"):]
        enum_body = enum_body[:enum_body.index("};")]
        self.assertLess(enum_body.index("StarvationInnFinished"),
                        enum_body.index("ReconFlagDeleted"))
        self.assertIn("std::vector<ReconMission> missions",
                      (ROOT / "src/ai/maxima/AIMaximaRecon.h").read_text())

    def test_scaling_and_confidence_rules_are_bounded(self):
        self.assertIn("age>=memoryHorizonTicks", self.recon)
        self.assertIn("age<=memoryHoldTicks", self.recon)
        self.assertIn("std::max(1, populationDivisor)", self.recon)
        self.assertIn("strategy.reconnaissance.mission_population_divisor",
                      self.source)
        self.assertIn("std::min(livingEnemies", self.recon)

    def test_economic_watch_is_late_game_and_observation_driven(self):
        planner = self.source[
            self.source.index("void Maxima::plan_reconnaissance_objectives") :
            self.source.index("bool Maxima::severe_food_emergency")
        ]
        update = self.source[
            self.source.index("void Maxima::update_reconnaissance") :
            self.source.index("void Maxima::update_opponent_models")
        ]
        self.assertIn("snapshot.prestige>0", planner)
        self.assertIn("lastEconomicSeenTick", planner)
        self.assertIn("watch_score, true", planner)
        self.assertIn("observeEconomicActivity", update)
        self.assertIn("isFOWDiscovered", update)

    def test_economic_watch_kind_and_memory_are_saved(self):
        self.assertIn('writeUint8(mission.economicWatch,"economic_watch")',
                      self.state)
        self.assertIn('readUint8("economic_watch")', self.state)
        self.assertIn('writeSint32(intel.lastEconomicSeenTick', self.state)
        self.assertIn('readSint32("last_economic_seen_tick")', self.state)


if __name__ == "__main__":
    unittest.main()
