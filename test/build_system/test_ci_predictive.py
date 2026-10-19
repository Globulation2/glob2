"""Temporal features and inert learned CI-selection contracts."""

import gzip
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".github/scripts"))
import ci_predictive as predictive


def history(*records):
    return {"schema": 1, "updated_at": 100, "records": list(records)}


def record(name, failed=0, end=10, branch="topic", event="pull_request", run_id=1):
    return {"name": name, "failed": failed, "end": end, "created_at": 0,
            "seconds": end, "branch": branch, "event": event,
            "run_id": run_id, "attempt": 1, "sha": "example", "paths": []}


def tree_model(feature="global_last_failed", threshold=.5):
    return {"schema": 1, "feature_schema": predictive.MODEL_FEATURE_SCHEMA,
            "model_id": "fixture-v1", "feature_names": [feature], "scales": [1.0],
            "trees": [{"left": [1, -1, -1], "right": [2, -1, -1],
                       "feature": [0, -2, -2], "threshold": [threshold, -2., -2.],
                       "probability": [.5, .8, .6]}]}


class PredictiveTest(unittest.TestCase):
    def test_committed_bundle_matches_inventory_and_works_outside_checkout(self):
        import hashlib
        import os
        from unittest.mock import patch
        import ci_changed_paths
        import ci_check_inventory
        manifest = json.loads((ROOT / ".github/ci/model-manifest.json").read_text())
        for name, expected in manifest["files"].items():
            self.assertEqual(hashlib.sha256((ROOT / ".github/ci" / name).read_bytes()).hexdigest(), expected)
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {}, clear=True):
            previous = Path.cwd()
            try:
                os.chdir(directory)
                data, model, reasons = ci_changed_paths._prediction_inputs()
            finally:
                os.chdir(previous)
        self.assertFalse(reasons)
        checks = [row for row in ci_check_inventory.inventory() if not row.get("full_only")]
        prediction = predictive.select_checks(checks, ["src/game/Game.cpp"], data, model,
            prediction_time=max(record["end"] for record in data["records"]) + 1)
        self.assertEqual(prediction["model_id"], manifest["model_id"])
        self.assertEqual(prediction["selected_count"], math.ceil(.12 * len(checks)))
        self.assertFalse(prediction["unknown_ids"])
        self.assertFalse(prediction["fallback_reasons"])

    def test_features_exclude_incomplete_and_future_outcomes(self):
        past = [record("job", 0, 10), record("job", 1, 20, "master", "push", 2)]
        future = [record("job", 0, 30, run_id=3), record("job", 0, 40, run_id=4)]
        check = {"id": "job"}
        expected = predictive.make_features(check, ["src/Game.cpp"], history(*past), branch="topic", prediction_time=30)
        actual = predictive.make_features(check, ["src/Game.cpp"], history(*(past + future)), branch="topic", prediction_time=30)
        self.assertEqual(actual, expected)
        self.assertEqual(actual["last_failed"], 1)
        self.assertAlmostEqual(actual["failure_rate"], 1.25 / 7)
        self.assertEqual(actual["branch_last_failed"], 0)
        self.assertEqual(actual["master_last_failed"], 1)
        self.assertEqual(actual["global_branch_disagree"], 1)
        self.assertEqual(actual["branch_known_global_failed"], 1)
        self.assertAlmostEqual(actual["streak_log_signed"], math.log(2))
        self.assertEqual(predictive.previous_outcome(check, history(*future), 30), None)

    def test_feature_schema_paths_rates_and_missingness(self):
        paths = ["src/game/Game.cpp", "src/game/Game.HPP", "README.md"]
        features = predictive.make_features("Game", paths, history(), prediction_time=100)
        self.assertEqual(features["unknown"], 1)
        self.assertEqual(features["paths_unknown"], 0)
        self.assertAlmostEqual(features["path_dirs_log"], math.log1p(3))
        self.assertAlmostEqual(features["fraction_ext:.hpp"], 1 / 3)
        self.assertAlmostEqual(features["changed_files_log"], math.log1p(3))
        self.assertAlmostEqual(features["failure_rate"], .05)
        self.assertEqual(features["event:pull_request"], 1)
        self.assertEqual(predictive.make_features("Game", None, history(), prediction_time=100)["paths_unknown"], 1)
        expected = {"last_failed", "failure_rate", "observations_log", "unknown", "semantic_overlap", "paths_unknown", "changed_files_log",
                    "rate_last_5", "rate_last_20", "streak_log_signed", "path_dirs_log", "global_branch_disagree", "branch_known_global_failed", "branch_global_rate_delta", "master_global_rate_delta"}
        expected |= {f"{prefix}_{suffix}" for prefix in ("global", "branch", "master") for suffix in ("known", "last_failed", "n_log", "rate", "age_log")}
        expected |= {f"{prefix}_{days}d{suffix}" for days in (1, 3, 7) for prefix, suffix in (("failures", "_log"), ("observations", "_log"), ("rate", ""))}
        expected |= {"fraction_ext:" + ext for ext in (".cpp", ".h", ".hpp", ".py", ".yml", ".yaml", ".js", ".ts", ".sh", ".md", ".txt")}
        expected |= {"event:" + event for event in ("pull_request", "push", "workflow_dispatch", "schedule")}
        self.assertEqual(predictive._allowed_features(), expected)
        self.assertEqual(len(expected), 54)

    def test_raw_aliases_match_reusable_prefix_without_substring_collisions(self):
        check = {"id": "current", "name": "Current check", "history_names": ["old check"]}
        data = history(record("Compiler / old check", 1), record("not old check", 0, 15, run_id=2))
        self.assertEqual(predictive.previous_outcome(check, data, 20), 1)
        self.assertEqual(predictive.make_features(check, [], data, prediction_time=20)["global_n_log"], math.log(2))

    def test_budget_unknown_priority_stable_ids_and_prior_discount(self):
        inventory = [{"id": "passed"}, {"id": "failed"}, {"id": "z-new"}, {"id": "a-new"}]
        data = history(record("passed"), record("failed", 1, run_id=2))
        selected = predictive.select_checks(inventory, [], data, tree_model(), budget=.25, prediction_time=30)
        self.assertEqual(selected["selected_ids"], ["a-new"])
        self.assertEqual(selected["rankings"], ["a-new", "z-new", "passed", "failed"])
        self.assertAlmostEqual(selected["probabilities"]["failed"], .6)
        self.assertAlmostEqual(selected["scores"]["failed"], .3)
        self.assertEqual(selected["unknown_ids"], ["a-new", "z-new"])
        self.assertIn("previous_failure_discount_0.5", selected["reasons"]["failed"])
        self.assertEqual(selected["rankings"], predictive.select_checks(list(reversed(inventory)), [], data, tree_model(), budget=.25, prediction_time=30)["rankings"])
        nine = [{"id": str(i)} for i in range(9)]
        rounded = predictive.select_checks(nine, [], history(), tree_model(), prediction_time=30)
        self.assertEqual(rounded["selected_count"], 2)
        self.assertAlmostEqual(rounded["actual_budget"], 2 / 9)
        self.assertEqual(predictive.select_checks([], [], history(), prediction_time=30)["selected_count"], 0)

    def test_model_gzip_and_float32_threshold_agree_with_tree_input_cast(self):
        value = .1000000009
        cast = struct.unpack("f", struct.pack("f", value))[0]
        model = tree_model("failure_rate", (value + cast) / 2)
        self.assertGreater(cast, value)
        self.assertEqual(predictive._probability({"failure_rate": value}, predictive.validate_model(model)), .6)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "model.json.gz"
            with gzip.open(path, "wt") as handle:
                json.dump(model, handle)
            self.assertEqual(predictive.load_model(path), model)
            result = predictive.select_checks([{"id": "job"}], [], history(record("job")), path, prediction_time=30)
            self.assertEqual(result["model_id"], "fixture-v1")
            # A valid gzip header with invalid deflate bytes raises zlib.error.
            path.write_bytes(bytes.fromhex("1f8b0800000000000003ff"))
            result = predictive.select_checks([{ "id": "job" }], [], history(record("job")), path, prediction_time=30)
            self.assertEqual(result["model_id"], "branch-recent-failure-fallback")
            path.write_bytes(b"corrupt")
            result = predictive.select_checks([{"id": "job"}], [], history(record("job")), path, prediction_time=30)
            self.assertEqual(result["model_id"], "branch-recent-failure-fallback")
            self.assertTrue(any(reason.startswith("invalid_model:") for reason in result["fallback_reasons"]))

    def test_corrupt_forests_fallback_without_traversal(self):
        variants = []
        cycle = tree_model();cycle["trees"][0]["left"][0] = 0;variants.append(cycle)
        mismatch = tree_model();mismatch["trees"][0]["probability"].pop();variants.append(mismatch)
        nan = tree_model();nan["trees"][0]["threshold"][0] = float("nan");variants.append(nan)
        bad_feature = tree_model();bad_feature["feature_names"] = ["unrecognized"];variants.append(bad_feature)
        bad_index = tree_model();bad_index["trees"][0]["feature"][0] = 99;variants.append(bad_index)
        tiny_scale = tree_model();tiny_scale["scales"] = [1e-300];variants.append(tiny_scale)
        inventory = [{"id": "passed"}, {"id": "failed"}]
        data = history(record("passed"), record("failed", 1, run_id=2))
        for model in variants:
            with self.subTest(model=model):
                result = predictive.select_checks(inventory, [], data, model, prediction_time=30)
                self.assertEqual(result["model_id"], "branch-recent-failure-fallback")
                self.assertEqual(result["selected_ids"], ["failed"])
                self.assertTrue(any(reason.startswith("invalid_model") for reason in result["fallback_reasons"]))

    def test_fallback_prefers_same_branch_over_conflicting_global_outcomes(self):
        data = history(record("a", 0, 10, "topic", run_id=1),
                       record("b", 1, 10, "topic", run_id=2),
                       record("a", 1, 20, "master", "push", 3),
                       record("b", 0, 20, "master", "push", 4))
        result = predictive.select_checks([{"id": "a"}, {"id": "b"}], [], data,
                                          budget=.5, branch="topic", prediction_time=30)
        self.assertEqual(result["selected_ids"], ["b"])
        self.assertGreater(result["scores"]["b"], 2)
        self.assertLess(result["scores"]["a"], 1)
        self.assertIn("branch_recent_failure_fallback", result["reasons"]["b"])
        other = predictive.select_checks([{"id": "a"}, {"id": "b"}], [], data,
                                         budget=.5, branch="new-topic", prediction_time=30)
        self.assertEqual(other["selected_ids"], ["a"])
        self.assertIn("global_recent_failure_fallback", other["reasons"]["a"])

    def test_invalid_inventory_budget_and_history(self):
        with self.assertRaises(ValueError):
            predictive.select_checks([{"id": "x"}, {"id": "x"}], [], history())
        for budget in (0, -1, 2, float("nan")):
            with self.assertRaises(ValueError):
                predictive.select_checks([], [], history(), budget=budget)
        result = predictive.select_checks([{"id": "new"}], [], None, prediction_time=30)
        self.assertEqual(result["unknown_ids"], ["new"])
        self.assertIn("missing_or_invalid_history", result["fallback_reasons"])
        dirty = history(record("job"), {"name": "job", "failed": 1})
        result = predictive.select_checks([{"id": "job"}], [], dirty, prediction_time=30)
        self.assertIn("ignored_invalid_history_records", result["fallback_reasons"])
        self.assertEqual(result["previous"]["job"], 0)


if __name__ == "__main__":
    unittest.main()
