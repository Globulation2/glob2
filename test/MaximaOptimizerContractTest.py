#!/usr/bin/env python3
"""Smoke tests proving optimizers consume the binary's strategy contract."""

import importlib
import importlib.util
import json
import math
from pathlib import Path
import random
from statistics import mean
import sys
import tempfile
import threading
import unittest


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))


class MaximaOptimizerContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        binaries = [path for path in (
            ROOT / "build-tournament/src/glob2",
            ROOT / "build/src/glob2",
        ) if path.is_file()]
        cls.binary = max(binaries, key=lambda path: path.stat().st_mtime) \
            if binaries else None
        if cls.binary is None:
            raise unittest.SkipTest("build glob2 before running optimizer contract tests")
        cls.optimizer = importlib.import_module("optimize_maxima")
        cls.optimizer.configure_parameters(cls.binary, "director", "2v2")

    def test_defaults_and_bounds_come_from_exported_schema(self) -> None:
        metadata = {
            item["key"]: item
            for item in self.optimizer.STRATEGY_SCHEMA["parameters"]
        }
        resolved = {
            item["key"]: item["value"]
            for item in self.optimizer.RESOLVED_STRATEGY["parameters"]
        }
        for parameter in self.optimizer.PARAMETERS:
            self.assertEqual(parameter.low, metadata[parameter.name]["searchMinimum"])
            self.assertEqual(parameter.high, metadata[parameter.name]["searchMaximum"])
            self.assertEqual(parameter.default, resolved[parameter.name])
            self.assertEqual(parameter.impact, metadata[parameter.name]["impact"])
            self.assertEqual(
                parameter.impact_rank, metadata[parameter.name]["impactRank"]
            )

    def test_invalid_candidate_fails_before_match_execution(self) -> None:
        candidate = self.optimizer.default_parameters()
        first = self.optimizer.PARAMETERS[0]
        candidate[first.name] = first.high + 1
        with self.assertRaises(ValueError):
            self.optimizer.tuning_string(candidate)

    @unittest.skipUnless(importlib.util.find_spec("optuna"), "Optuna not installed")
    def test_optuna_multivariate_proposals_do_not_collapse_to_boundaries(self) -> None:
        self.optimizer.configure_parameters(self.binary, "tactical", "2v2")
        self.addCleanup(
            self.optimizer.configure_parameters, self.binary, "director", "2v2"
        )
        source_rng = random.Random(1001)
        records = []
        for index in range(48):
            parameters = self.optimizer.random_parameters(source_rng)
            # A smooth but interacting synthetic objective gives the sampler a
            # stable signal without depending on tournament artifacts.
            attack = parameters["military.offense_warrior_base_percent"]
            reserve = parameters["military.defense_reserve_floor"]
            stall = parameters["scheduling.campaign_stall_ticks"]
            cooldown = parameters["scheduling.campaign_retreat_cooldown_ticks"]
            objective = -abs(attack * 10 - reserve) / 1000.0
            objective -= abs(stall - cooldown) / 1000000.0
            records.append({
                "trial": index, "parameters": parameters,
                "objective": objective, "failed": 0,
            })

        proposal_rng = random.Random(2002)
        bounds = {
            parameter.name: (parameter.low, parameter.high)
            for parameter in self.optimizer.PARAMETERS
        }
        proposals = [
            self.optimizer.optuna_tpe_parameters(
                proposal_rng, records, startup_trials=24
            )
            for _ in range(16)
        ]
        boundary_counts = [
            sum(value in bounds[name] for name, value in proposal.items())
            for proposal in proposals
        ]
        # With deliberately local integer windows, a boundary hit is naturally
        # more common than it was across the old hard-safety domains. Compare
        # against the exact uniform-sampling expectation for these domains;
        # collapse means doing materially worse than that baseline.
        uniform_boundary_expectation = sum(
            1.0 if high == low else min(1.0, 2.0 / (high - low + 1))
            for low, high in bounds.values()
        )
        self.assertEqual(len({tuple(row.items()) for row in proposals}), 16)
        self.assertLess(mean(boundary_counts), uniform_boundary_expectation)
        self.assertLessEqual(
            max(boundary_counts), math.ceil(uniform_boundary_expectation * 2)
        )

    @unittest.skipUnless(importlib.util.find_spec("optuna"), "Optuna not installed")
    def test_optuna_proposals_are_seed_reproducible(self) -> None:
        self.optimizer.configure_parameters(self.binary, "tactical", "2v2")
        self.addCleanup(
            self.optimizer.configure_parameters, self.binary, "director", "2v2"
        )
        source_rng = random.Random(3003)
        records = [
            {
                "parameters": self.optimizer.random_parameters(source_rng),
                "objective": index / 32.0,
                "failed": 0,
            }
            for index in range(32)
        ]
        left = self.optimizer.optuna_tpe_parameters(
            random.Random(4004), records, startup_trials=24
        )
        right = self.optimizer.optuna_tpe_parameters(
            random.Random(4004), records, startup_trials=24
        )
        self.assertEqual(left, right)

    @unittest.skipUnless(importlib.util.find_spec("optuna"), "Optuna not installed")
    def test_custom_tpe_checkpoint_is_rejected(self) -> None:
        portfolio = importlib.import_module("optimize_maxima_portfolio")
        payload = {
            "metadata": {
                "bohb": True,
                "bohb_budgets": [49, 147, 440],
                "strategy_schema_version": 2,
                "score_signal": {"version": portfolio.scoring.SIGNAL_VERSION},
                "proposal_engine": "custom-coordinate-tpe-v1",
                "optuna_version": self.optimizer.optuna_version(),
            },
            "trials": [], "bohb_rungs": [],
        }
        with self.assertRaisesRegex(RuntimeError, "proposal engine is incompatible"):
            portfolio.validate_warm_start(payload)

    @unittest.skipUnless(importlib.util.find_spec("optuna"), "Optuna not installed")
    def test_optuna_checkpoint_replay_matches_batched_proposals(self) -> None:
        portfolio = importlib.import_module("optimize_maxima_portfolio")
        self.optimizer.configure_parameters(self.binary, "tactical", "2v2")
        self.addCleanup(
            self.optimizer.configure_parameters, self.binary, "director", "2v2"
        )
        seed = 5005
        rng = random.Random(seed)
        observations = {49: [], 147: [], 440: []}
        configurations = []
        trials = []
        evaluations = []
        brackets = []
        next_trial = 0
        for bracket_id in range(2):
            trial_ids = []
            pending = []
            for _ in range(3):
                trial_id = next_trial
                next_trial += 1
                if trial_id == 0:
                    parameters = self.optimizer.default_parameters()
                else:
                    model_budget, model_records = portfolio.bohb_model_records(
                        observations, 1
                    )
                    self.assertGreater(rng.random(), 0.0)
                    parameters = (
                        portfolio.propose_bohb_parameters(
                            rng, model_records, configurations, 0.0, 1
                        )
                        if model_budget is not None
                        else self.optimizer.random_parameters(rng)
                    )
                stored = {"trial": trial_id, "parameters": parameters}
                configurations.append(stored)
                trials.append(stored)
                trial_ids.append(trial_id)
                pending.append({
                    "trial": trial_id, "bracket": bracket_id,
                    "budget_games": 49, "parameters": parameters,
                    "objective": trial_id / 10.0, "failed": 0,
                })
            observations[49].extend(pending)
            evaluations.extend(pending)
            brackets.append({
                "bracket": bracket_id, "start_budget": 49,
                "new_trials": trial_ids, "promotions": [],
            })
        payload = {
            "trials": trials,
            "bohb_rungs": [evaluations, [], []],
            "bohb_brackets": brackets,
        }
        portfolio.restore_bohb_rng(
            random.Random(seed), payload, startup_trials=1,
            random_fraction=0.0, required_distance=0.0,
        )

    def test_tactical_flag_inputs_respect_engine_limits(self) -> None:
        self.optimizer.configure_parameters(self.binary, "tactical", "2v2")
        parameters = {item.name: item for item in self.optimizer.PARAMETERS}
        self.assertLessEqual(parameters["defense.reactive.flag_radius"].high, 32)
        self.assertLessEqual(parameters["fruit.flag_radius"].high, 32)
        for key in (
            "defense.reactive.unit_cap",
            "explorer_campaign.units_per_flag",
            "fruit.units_per_flag",
        ):
            self.assertLessEqual(parameters[key].high, 20)

    def test_farming_stage_includes_independent_behavior_switches(self) -> None:
        self.optimizer.configure_parameters(self.binary, "farming", "ffa5plus")
        self.addCleanup(
            self.optimizer.configure_parameters, self.binary, "director", "2v2"
        )
        selected = {parameter.name for parameter in self.optimizer.PARAMETERS}
        for key in (
            "farming.coastal_porosity_enabled",
            "farming.resource_preserving_circulation_enabled",
            "farming.wheat_invasion_clearing_enabled",
            "farming.wood_firebreak_enabled",
        ):
            self.assertIn(key, selected)

    def test_tactical_core_is_limited_to_warrior_campaigns_and_raiding(self) -> None:
        self.optimizer.configure_parameters(self.binary, "tactical-core", "2v2")
        self.addCleanup(
            self.optimizer.configure_parameters, self.binary, "director", "2v2"
        )
        self.assertEqual(
            {parameter.name for parameter in self.optimizer.PARAMETERS},
            {
                "military.defense_reserve_floor",
                "military.defense_enemy_percent",
                "military.offense_warrior_base_percent",
                "recon.offense_population_divisor",
                "military.campaign_deployable_min",
                "scheduling.campaign_stall_ticks",
                "scheduling.campaign_retreat_cooldown_ticks",
                "scoring.target_switch_margin",
                "scoring.target_reachable_weight",
                "scoring.target_warrior_weight",
                "scoring.target_distance_bias",
                "fruit.population_min",
                "fruit.units_per_flag",
                "fruit.flag_radius",
            },
        )

    def test_teamplay_stage_contains_every_teamplay_parameter(self) -> None:
        self.optimizer.configure_parameters(self.binary, "teamplay", "2v2")
        self.addCleanup(
            self.optimizer.configure_parameters, self.binary, "director", "2v2"
        )
        selected = {parameter.name for parameter in self.optimizer.PARAMETERS}
        schema = {
            item["key"]
            for item in self.optimizer.STRATEGY_SCHEMA["parameters"]
            if item["group"] == "teamplay"
        }
        self.assertEqual(selected, schema)

    def test_candidate_resolves_with_canonical_provenance(self) -> None:
        candidate = self.optimizer.default_parameters()
        result = self.optimizer.resolve_candidate(self.binary, candidate)
        entries = {item["key"]: item for item in result["parameters"]}
        for parameter in self.optimizer.PARAMETERS:
            self.assertEqual(entries[parameter.name]["value"], candidate[parameter.name])
            self.assertTrue(
                entries[parameter.name]["source"].startswith("command-line overrides:")
            )
        self.assertEqual(result["schemaVersion"], 2)

    def test_portfolio_warm_start_requires_schema_v2(self) -> None:
        portfolio = importlib.import_module("optimize_maxima_portfolio")
        with tempfile.TemporaryDirectory() as directory:
            result = Path(directory) / "optimization.json"
            result.write_text(json.dumps({
                "metadata": {
                    "bohb": True,
                    "bohb_budgets": [49, 147, 440],
                    "strategy_schema_version": 1,
                }
            }))
            payload, _ = portfolio.load_warm_start(result)
            with self.assertRaisesRegex(RuntimeError, "schema version must be 2"):
                portfolio.validate_warm_start(payload)

    def test_checkpoint_recovery_uses_only_completed_brackets(self) -> None:
        portfolio = importlib.import_module("optimize_maxima_portfolio")
        with tempfile.TemporaryDirectory() as directory:
            checkpoint = Path(directory)
            (checkpoint / "run-manifest.json").write_text(json.dumps({
                "strategy_schema": {"schemaVersion": 2},
                "seeds": {"search": 1, "sampler": 2},
            }))
            records = [
                {"trial": 0, "bracket": 0, "budget_games": 49,
                 "score_version": 1},
                {"trial": 1, "bracket": 1, "budget_games": 49,
                 "score_version": 1},
            ]
            (checkpoint / "trials.jsonl").write_text("".join(
                json.dumps(record) + "\n" for record in records
            ))
            (checkpoint / "bohb-evaluations.jsonl").write_text("".join(
                json.dumps(record) + "\n" for record in records
            ))
            (checkpoint / "bohb-brackets.json").write_text(json.dumps([
                {"bracket": 0, "start_budget": 49, "new_trials": [0],
                 "promotions": []},
            ]))
            payload, _ = portfolio.load_warm_start(checkpoint)
            self.assertEqual([record["trial"] for record in payload["trials"]], [0])
            self.assertEqual(len(payload["bohb_rungs"][0]), 1)
            self.assertEqual(payload["metadata"]["strategy_schema_version"], 2)

    def test_portfolio_cluster_groups_mixed_configuration_results(self) -> None:
        portfolio = importlib.import_module("optimize_maxima_portfolio")

        class FakeCluster(portfolio.PortfolioCluster):
            def __init__(self) -> None:
                self.workers = [("local", 2)]
                self.slots = [("local", 0), ("local", 0)]
                self.retries = 0
                self.require_all_workers = False
                self.connection_epoch = 0
                self.prepared_connections = set()
                self.seen = []
                self.lock = threading.Lock()

            def _run_one(self, host, group, match, settings):
                with self.lock:
                    self.seen.append((match["id"], settings))
                return {**match, "status": "completed"}

        cluster = FakeCluster()
        results = cluster.run_many([
            (10, [{"id": 2}, {"id": 0}], "alpha"),
            (20, [{"id": 1}], "beta"),
        ])
        self.assertEqual([row["id"] for row in results[10]], [0, 2])
        self.assertEqual([row["id"] for row in results[20]], [1])
        self.assertCountEqual(
            cluster.seen, [(2, "alpha"), (0, "alpha"), (1, "beta")]
        )

    def test_ssh_failure_quarantines_only_one_connection_group(self) -> None:
        portfolio = importlib.import_module("optimize_maxima_portfolio")

        class FakeRemoteCluster(portfolio.PortfolioCluster):
            def __init__(self) -> None:
                self.workers = [("remote", 8)]
                self.slots = [
                    ("remote", index) for index in range(8)
                ]
                self.retries = 0
                self.require_all_workers = False
                self.connection_epoch = 0
                self.prepared_connections = set()

            def _prepare_connection(self, host, group):
                return True

            def _reset_connection(self, host, group):
                return None

            def _run_one(self, host, group, match, settings):
                if group == 0:
                    return {
                        **match, "status": "failed", "returncode": 255,
                    }
                return {
                    **match, "status": "completed", "returncode": 0,
                    "group": group,
                }

        cluster = FakeRemoteCluster()
        schedule = [{"id": index} for index in range(12)]
        results = cluster.run_many([(1, schedule, "settings")])[1]
        self.assertEqual(len(results), len(schedule))
        self.assertTrue(all(row["status"] == "completed" for row in results))
        self.assertTrue(all(row["group"] == 1 for row in results))


if __name__ == "__main__":
    unittest.main()
