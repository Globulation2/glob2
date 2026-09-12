#!/usr/bin/env python3
"""Structural acceptance checks for Maxima reconnaissance integration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MaximaReconPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "src/AIMaxima.h").read_text()
        cls.source = (ROOT / "src/AIMaxima.cpp").read_text()
        cls.recon = (ROOT / "src/AIMaximaRecon.cpp").read_text()
        cls.runtime = (ROOT / "src/AIMaximaRuntime.h").read_text()

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

    def test_save_version_and_legacy_recon_gates(self):
        version = (ROOT / "src/Version.h").read_text()
        self.assertIn("#define VERSION_MINOR 99", version)
        loader = self.source[self.source.index("bool Maxima::loadDirector"):]
        loader = loader[:loader.index("bool Maxima::load(")]
        # The mission section is the one part of the director state that is
        # version gated; reconnaissance is always rebuilt from the save.
        self.assertIn("if(versionMinor>=98)", loader)
        self.assertIn("stream->readEnterSection(\"Recon\")", loader)
        self.assertIn("reconnaissance.reset()", loader)

    def test_flag_event_is_appended_and_owned_separately(self):
        enum_body = self.runtime[self.runtime.index("enum Type"):]
        enum_body = enum_body[:enum_body.index("};")]
        self.assertLess(enum_body.index("StarvationInnFinished"),
                        enum_body.index("ReconFlagDeleted"))
        self.assertIn("std::vector<ReconMission> missions", 
                      (ROOT / "src/AIMaximaRecon.h").read_text())

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
                      self.source)
        self.assertIn('readUint8("economic_watch")', self.source)
        self.assertIn('writeSint32(intel.lastEconomicSeenTick', self.source)
        self.assertIn('readSint32("last_economic_seen_tick")', self.source)


if __name__ == "__main__":
    unittest.main()
