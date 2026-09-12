#!/usr/bin/env python3
"""Black-box tests for the Maxima strategy configuration contract."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


def find_binary() -> Path | None:
    candidates = [candidate for candidate in (
        ROOT / "build-tournament/src/glob2",
        ROOT / "build/src/glob2",
    ) if candidate.is_file()]
    return max(candidates, key=lambda candidate: candidate.stat().st_mtime) \
        if candidates else None


class MaximaStrategyConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.binary = find_binary()
        if cls.binary is None:
            raise unittest.SkipTest("build glob2 before running strategy integration tests")
        schema = cls.run_binary("--dump-maxima-schema")
        cls.schema = json.loads(schema.stdout)
        resolved = cls.run_binary(
            "--dump-maxima-strategy", "--maxima-format", "duel"
        )
        cls.resolved = json.loads(resolved.stdout)

    @classmethod
    def run_binary(cls, *args: str, env_overrides=None) -> subprocess.CompletedProcess:
        environment = os.environ.copy()
        environment.pop("GLOB2_MAXIMA_TUNING", None)
        environment.pop("GLOB2_MAXIMA_OVERRIDES", None)
        environment.pop("GLOB2_NICOWAR_V3_TUNING", None)
        environment.pop("GLOB2_NICOWAR_V3_OVERRIDES", None)
        if env_overrides:
            environment.update(env_overrides)
        return subprocess.run(
            [str(cls.binary), *args],
            cwd=ROOT,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )

    @classmethod
    def resolve(cls, *args: str, env_overrides=None) -> dict:
        result = cls.run_binary(
            "--dump-maxima-strategy",
            "--maxima-format",
            "duel",
            *args,
            env_overrides=env_overrides,
        )
        return json.loads(result.stdout)

    @staticmethod
    def values(payload: dict) -> dict:
        return {item["key"]: item["value"] for item in payload["parameters"]}

    def test_offensive_muster_defaults_and_overrides(self) -> None:
        keys = ("tactics.siege_muster_percent", "raiding.muster_percent")
        defaults = self.values(self.resolved)
        self.assertEqual([50, 50], [defaults[key] for key in keys])
        changed = self.values(self.resolve("--maxima-overrides",
            "tactics.siege_muster_percent=75,raiding.muster_percent=60"))
        self.assertEqual([75, 60], [changed[key] for key in keys])

    def test_schema_base_and_round_trip_are_bijective(self) -> None:
        self.assertEqual(self.schema["schemaVersion"], 2)
        self.assertEqual(self.resolved["schemaVersion"], 2)
        metadata = self.schema["parameters"]
        keys = [item["key"] for item in metadata]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(len(keys), len(self.resolved["parameters"]))
        self.assertTrue(any(item["type"] == "boolean" for item in metadata))
        self.assertTrue(any(item["type"] == "integer" for item in metadata))
        resolved_values = self.values(self.resolved)
        narrowed = 0
        impact_ranks = {"low": 1, "medium": 2, "high": 3, "critical": 4}
        observed_impacts = set()
        for item in metadata:
            self.assertTrue(item["group"])
            self.assertTrue(item["unit"])
            self.assertTrue(item["description"])
            self.assertIn(item["impact"], impact_ranks)
            self.assertEqual(item["impactRank"], impact_ranks[item["impact"]])
            observed_impacts.add(item["impact"])
            self.assertLessEqual(item["hardMinimum"], item["searchMinimum"])
            self.assertLessEqual(item["searchMinimum"], resolved_values[item["key"]])
            self.assertLessEqual(resolved_values[item["key"]], item["searchMaximum"])
            self.assertLessEqual(item["searchMaximum"], item["hardMaximum"])
            narrowed += (
                item["searchMinimum"] > item["hardMinimum"]
                or item["searchMaximum"] < item["hardMaximum"]
            )
        self.assertGreater(narrowed, len(metadata) * 3 // 4)
        self.assertEqual(observed_impacts, set(impact_ranks))

        self.assertEqual(resolved_values["military.preemptive_warriors_min"], 4)
        self.assertEqual(
            resolved_values["military.preemptive_warriors_per_zone"], 3
        )
        self.assertTrue(
            resolved_values["military.preemptive_amphibious_enabled"]
        )
        self.assertEqual(
            resolved_values["military.preemptive_swimming_warriors_min"], 4
        )
        self.assertEqual(resolved_values["military.preemptive_probe_radius"], 5)
        self.assertEqual(
            resolved_values["military.preemptive_cross_section_max"], 10
        )
        self.assertEqual(resolved_values["military.preemptive_zone_radius"], 3)
        self.assertEqual(resolved_values["military.preemptive_zone_max"], 6)
        self.assertEqual(resolved_values["recon.memory_horizon_ticks"], 10000)
        self.assertEqual(resolved_values["recon.force_memory_hold_ticks"], 2500)
        self.assertEqual(resolved_values["recon.force_sample_interval_ticks"], 10)
        self.assertEqual(resolved_values["recon.force_sample_phase_offset_ticks"], 1)
        self.assertEqual(
            resolved_values["staffing.swarm_supply_radius"], 12
        )

        assignments = ",".join(
            f'{item["key"]}={str(item["value"]).lower()}'
            for item in self.resolved["parameters"]
        )
        round_trip = self.resolve("--maxima-overrides", assignments)
        self.assertEqual(self.values(round_trip), self.values(self.resolved))

    def test_tactic_switches_default_on_and_accept_false(self) -> None:
        expected = {
            "economy.swarm_retirement_enabled",
            "economy.large_economy_adaptation_enabled",
            "economy.amphibious_network_maintenance_enabled",
            "economy.food_service_safeguards_enabled",
            "economy.worker_birth_throttle_enabled",
            "upgrades.enabled",
            "repairs.enabled",
            "military.counterattack_enabled",
            "military.explorer_defense_enabled",
            "military.warrior_training_backlog_throttle_enabled",
            "postures.recover_enabled",
            "postures.defend_enabled",
            "postures.expand_enabled",
            "postures.develop_enabled",
            "postures.mobilize_enabled",
            "postures.campaign_enabled",
            "postures.finish_enabled",
            "placement.food_preservation_enabled",
            "placement.defensive_siting_enabled",
            "placement.spacing_compactness_enabled",
            "placement.artery_routing_enabled",
            "defense.reactive.enabled",
            "tactics.enabled",
            "tactics.siege_enabled",
            "tactics.dig_out_enabled",
            "tactics.failed_target_quarantine_enabled",
            "tactics.siege_target_lock_enabled",
            "raiding.enabled",
            "teamplay.enabled",
            "teamplay.pressure_coordination_enabled",
            "explorer_campaign.enabled",
            "fruit.enabled",
            "recon.enabled",
            "recon.scouting_missions_enabled",
            "recon.economic_watch_enabled",
            "recon.force_memory_enabled",
            "farming.enabled",
            "farming.farm_protection_enabled",
            "farming.maintenance_clearing_enabled",
            "farming.resource_preserving_circulation_enabled",
            "farming.wheat_invasion_clearing_enabled",
            "farming.wood_firebreak_enabled",
            "farming.proactive_clearing_enabled",
            "emergencies.food_enabled",
            "emergencies.colony_enabled",
        }
        metadata = {item["key"]: item for item in self.schema["parameters"]}
        values = self.values(self.resolved)
        self.assertTrue(expected.issubset(metadata))
        self.assertTrue(all(metadata[key]["type"] == "boolean" for key in expected))
        self.assertTrue(all(values[key] is True for key in expected))

        # Keep one posture enabled to satisfy the posture-set invariant.
        disabled = expected - {"postures.recover_enabled"}
        overrides = ",".join(f"{key}=false" for key in sorted(disabled))
        switched = self.values(self.resolve("--maxima-overrides", overrides))
        self.assertTrue(all(switched[key] is False for key in disabled))
        self.assertTrue(switched["postures.recover_enabled"])

    def test_at_least_one_posture_must_remain_enabled(self) -> None:
        posture_keys = (
            "recover", "defend", "expand", "develop", "mobilize",
            "campaign", "finish",
        )
        overrides = ",".join(
            f"postures.{name}_enabled=false" for name in posture_keys
        )
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.resolve("--maxima-overrides", overrides)
        self.assertIn("at least one postures.*_enabled", raised.exception.stderr)

    def test_legacy_contract_is_rejected_without_aliases(self) -> None:
        legacy_keys = (
            "phases.growth_population_max",
            "economy.inn_level1_capacity",
            "production.upgrade1_population_min",
            "construction.base_sites",
            "military.attack_flag_count",
            "recon.offense_explorer_min",
            "adaptation.weight_step",
        )
        schema_keys = {item["key"] for item in self.schema["parameters"]}
        self.assertTrue(schema_keys.isdisjoint(legacy_keys))
        for key in legacy_keys:
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                self.resolve("--maxima-overrides", f"{key}=1")
            self.assertIn("unknown Maxima key", raised.exception.stderr)

    def test_old_cli_and_environment_names_report_migration(self) -> None:
        old_cli = subprocess.run(
            [str(self.binary), "--dump-nicowar-v3-schema"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertNotEqual(old_cli.returncode, 0)
        self.assertIn("renamed for Maxima", old_cli.stderr)
        self.assertIn("--maxima-", old_cli.stderr)

        for variable in (
            "GLOB2_NICOWAR_V3_OVERRIDES",
            "GLOB2_NICOWAR_V3_TUNING",
        ):
            environment = os.environ.copy()
            environment[variable] = "economy.inn_population_offset=1"
            old_env = subprocess.run(
                [
                    str(self.binary),
                    "--dump-maxima-strategy",
                    "--maxima-format",
                    "duel",
                ],
                cwd=ROOT,
                env=environment,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertNotEqual(old_env.returncode, 0)
            self.assertIn("GLOB2_MAXIMA_OVERRIDES", old_env.stderr)

    def test_all_format_files_exist_and_may_be_empty(self) -> None:
        for format_name in ("duel", "ffa3", "ffa4", "ffa5plus", "2v2"):
            payload = json.loads(
                self.run_binary(
                    "--dump-maxima-strategy",
                    "--maxima-format",
                    format_name,
                ).stdout
            )
            self.assertEqual(payload["format"], format_name)
            self.assertEqual(payload["sources"][1], f"data/maxima/{format_name}.strategy")

    def test_match_format_inference_and_explicit_override(self) -> None:
        scenarios = (
            ("duel", "maps/FourSquares1.map", 2),
            ("ffa3", "maps/FourSquares1.map", 3),
            ("ffa4", "maps/FourSquares1.map", 4),
            ("ffa5plus", "maps/Archipelago.map", 5),
        )
        for expected, map_file, players in scenarios:
            result = self.run_binary(
                "-nicowar-scenario-match-nox",
                map_file,
                "123",
                str(players),
                "7",
                "5",
                "0",
                "0",
                "1",
            )
            self.assertIn(f"Maxima strategy: format={expected} ", result.stderr)

        result = self.run_binary(
            "-nicowar-2v2-match-nox",
            "maps/FourSquares1.map",
            "123",
            "7",
            "5",
            "0",
            "0",
            "1",
        )
        self.assertIn("Maxima strategy: format=2v2 ", result.stderr)
        # Per-player policy resolution is intentional: checkpoint experiments
        # may override one allied Maxima without changing its teammate.
        self.assertEqual(result.stderr.count("Maxima strategy:"), 2)
        self.assertIn("player=0 team=0", result.stderr)
        self.assertIn("player=1 team=1", result.stderr)

        explicit = self.run_binary(
            "--maxima-format",
            "duel",
            "-nicowar-scenario-match-nox",
            "maps/FourSquares1.map",
            "123",
            "4",
            "7",
            "5",
            "0",
            "0",
            "1",
        )
        self.assertIn("Maxima strategy: format=duel ", explicit.stderr)

    def test_precedence_and_per_key_provenance(self) -> None:
        key = "economy.inn_population_offset"
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.strategy"
            second = Path(directory) / "second.strategy"
            first.write_text(f"{key} = 40\n", encoding="utf-8")
            second.write_text(f"# later layer\n{key} = 50\n", encoding="utf-8")
            payload = self.resolve(
                "--maxima-layer",
                str(first),
                "--maxima-layer",
                str(second),
                "--maxima-overrides",
                f"{key}=60",
                env_overrides={"GLOB2_MAXIMA_OVERRIDES": f"{key}=70"},
            )
        entry = next(item for item in payload["parameters"] if item["key"] == key)
        self.assertEqual(entry["value"], 70)
        self.assertEqual(entry["source"], "GLOB2_MAXIMA_OVERRIDES:1")

    def test_player_override_changes_only_the_focal_maxima(self) -> None:
        result = self.run_binary(
            "-nicowar-2v2-match-nox", "maps/FourSquares1.map", "123",
            "7", "5", "0", "0", "1",
            "--maxima-player-overrides", "0", "farming.enabled=false",
        )
        lines = [line for line in result.stderr.splitlines()
                 if line.startswith("Maxima strategy:")]
        focal = next(line for line in lines if "player=0 team=0" in line)
        teammate = next(line for line in lines if "player=1 team=1" in line)
        self.assertIn("farming.enabled=false", focal)
        self.assertIn("farming.enabled=true", teammate)

    def test_empty_sparse_layer_and_bool_are_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            layer = Path(directory) / "empty.strategy"
            layer.write_text("# intentionally empty\n", encoding="utf-8")
            payload = self.resolve(
                "--maxima-layer",
                str(layer),
                "--maxima-overrides",
                "military.preemptive_defense_enabled=false",
            )
        self.assertFalse(self.values(payload)["military.preemptive_defense_enabled"])

    def assert_layer_error(self, contents: str, expected: str) -> None:
        with tempfile.TemporaryDirectory() as directory:
            layer = Path(directory) / "bad.strategy"
            layer.write_text(contents, encoding="utf-8")
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                self.resolve("--maxima-layer", str(layer))
        self.assertIn(f"{layer}:", raised.exception.stderr)
        self.assertIn(expected, raised.exception.stderr)

    def test_precise_syntax_duplicate_unknown_type_and_range_errors(self) -> None:
        self.assert_layer_error("not an assignment\n", "expected one 'key = value'")
        self.assert_layer_error(
            "economy.inn_population_offset=1\neconomy.inn_population_offset=2\n",
            "duplicate key",
        )
        self.assert_layer_error("unknown.parameter=1\n", "unknown Maxima key")
        self.assert_layer_error(
            "military.preemptive_defense_enabled=maybe\n", "expected boolean"
        )
        self.assert_layer_error(
            "economy.inn_population_offset=201\n", "is outside"
        )

    def test_incomplete_base_and_relational_failures_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / "incomplete.strategy"
            base.write_text("economy.inn_population_offset=1\n", encoding="utf-8")
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                self.resolve("--maxima-base", str(base))
            self.assertIn("required base strategy is missing key", raised.exception.stderr)

        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.resolve(
                "--maxima-overrides",
                "economy.food_headroom_critical=80,economy.food_headroom_warning=20",
            )
        self.assertIn("must be <=", raised.exception.stderr)

        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.resolve(
                "--maxima-overrides",
                "teamplay.defense_contact_ttl_ticks=2000,"
                "teamplay.defense_max_engagement_ticks=1000",
            )
        self.assertIn("teamplay.defense_contact_ttl_ticks", raised.exception.stderr)

        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.resolve(
                "--maxima-overrides",
                "defense.reactive.move_deadband=13,"
                "defense.reactive.move_radius=12",
            )
        self.assertIn("defense.reactive.move_deadband", raised.exception.stderr)

        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.resolve(
                "--maxima-overrides",
                "recon.force_memory_hold_ticks=10000,"
                "recon.memory_horizon_ticks=10000",
            )
        self.assertIn("recon.force_memory_hold_ticks", raised.exception.stderr)

        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.resolve(
                "--maxima-overrides",
                "recon.force_sample_interval_ticks=10,"
                "recon.force_sample_phase_offset_ticks=10",
            )
        self.assertIn("recon.force_sample_phase_offset_ticks", raised.exception.stderr)

    def test_old_environment_variable_is_an_explicit_error(self) -> None:
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.resolve(env_overrides={"GLOB2_MAXIMA_TUNING": "x=1"})
        self.assertIn("is no longer supported", raised.exception.stderr)


if __name__ == "__main__":
    unittest.main()
