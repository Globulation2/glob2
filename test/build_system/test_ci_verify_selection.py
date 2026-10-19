"""Required-gate inventory, matrix-leaf and exact-attempt verification."""

import copy
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".github/scripts"))
import ci_check_inventory as inventory
import ci_verify_selection as gate


def plan_and_needs(selected):
    plan = inventory.execution_plan(selected)
    plan.update(verification_mode="predictive", run_attempt=2)
    needs = {job: {"result": "success" if take else "skipped"} for job, take in plan["jobs"].items()}
    needs["changes"] = {"result": "success"}
    return plan, needs


def successful_jobs(plan):
    return [{"id": index + 100, "name": check["name"], "status": "completed",
             "conclusion": "success", "run_attempt": 2}
            for index, check in enumerate(plan["expected_checks"])]


class VerifySelectionTest(unittest.TestCase):
    def test_empty_cheap_plan_requires_only_declared_top_results(self):
        plan, needs = plan_and_needs([])
        self.assertTrue(gate.verify(needs, plan)["ok"])
        needs["changes"]["result"] = "failure"
        self.assertFalse(gate.verify(needs, plan)["ok"])
        self.assertFalse(gate.verify({}, None)["ok"])
        self.assertFalse(gate.verify(needs, {"schema": 1})["ok"])

    def test_selected_matrix_leaf_missing_even_if_parent_success(self):
        plan, needs = plan_and_needs(["native/gcc13/shard-1"])
        jobs = successful_jobs(plan)
        self.assertTrue(gate.verify(needs, plan, jobs)["ok"])
        missing = [job for job in jobs if "Linux tests" not in job["name"]]
        result = gate.verify(needs, plan, missing)
        self.assertFalse(result["ok"])
        self.assertTrue(any("native/gcc13/shard-1" in error for error in result["errors"]))
        self.assertFalse(gate.verify(needs, plan)["ok"])
        needs["linux-gcc13"]["result"] = "skipped"
        self.assertFalse(gate.verify(needs, plan, jobs)["ok"])

    def test_unselected_skipped_is_accepted_but_unplanned_execution_rejected(self):
        plan, needs = plan_and_needs(["windows"])
        jobs = successful_jobs(plan)
        self.assertTrue(gate.verify(needs, plan, jobs)["ok"])
        self.assertEqual(needs["android"]["result"], "skipped")
        needs["android"]["result"] = "success"
        self.assertFalse(gate.verify(needs, plan, jobs)["ok"])

    def test_failure_cancellation_skip_and_pending_cannot_turn_green(self):
        plan, needs = plan_and_needs(["windows"])
        for outcome in ("failure", "cancelled", "skipped", "neutral", None):
            with self.subTest(outcome=outcome):
                jobs = successful_jobs(plan)
                jobs[0]["conclusion"] = outcome
                self.assertFalse(gate.verify(needs, plan, jobs)["ok"])
        jobs = successful_jobs(plan);jobs[0]["status"] = "in_progress"
        self.assertFalse(gate.verify(needs, plan, jobs)["ok"])
        for outcome in ("failure", "cancelled", "skipped"):
            needs["windows"]["result"] = outcome
            self.assertFalse(gate.verify(needs, plan, successful_jobs(plan))["ok"])

    def test_plan_fields_cannot_hide_missing_jobs_or_dependencies(self):
        original, needs = plan_and_needs(["native/gcc13/shard-1"])
        mutations = []
        missing_expected = copy.deepcopy(original);missing_expected["expected_checks"].pop();mutations.append(missing_expected)
        missing_executed = copy.deepcopy(original);missing_executed["executed_ids"].pop();mutations.append(missing_executed)
        wrong_flag = copy.deepcopy(original);wrong_flag["jobs"]["windows"] = True;mutations.append(wrong_flag)
        wrong_option = copy.deepcopy(original);wrong_option["options"]["gcc13_shards"] = [];mutations.append(wrong_option)
        missing_flag = copy.deepcopy(original);del missing_flag["jobs"]["windows"];mutations.append(missing_flag)
        wrong_alias = copy.deepcopy(original);wrong_alias["expected_checks"][0]["name"] = "unrelated success";mutations.append(wrong_alias)
        missing_dep = copy.deepcopy(original)
        missing_dep["executed_ids"] = ["native/gcc13/shard-1"]
        missing_dep["dependency_ids"] = []
        missing_dep["expected_checks"] = [check for check in missing_dep["expected_checks"] if check["id"].endswith("shard-1")]
        mutations.append(missing_dep)
        for plan in mutations:
            with self.subTest(plan=plan):
                self.assertFalse(gate.verify(needs, plan, successful_jobs(original))["ok"])

    def test_reusable_aliases_and_old_attempt_exclusion(self):
        plan, needs = plan_and_needs(["windows"])
        current = successful_jobs(plan)
        current[0]["name"] = "Reusable Windows / " + current[0]["name"]
        old = dict(current[0], id=999, run_attempt=1, conclusion="failure")
        self.assertTrue(gate.verify(needs, plan, [old] + current)["ok"])
        self.assertFalse(gate.verify(needs, plan, [old])["ok"])
        duplicate = dict(current[0], id=888)
        self.assertFalse(gate.verify(needs, plan, current + [duplicate])["ok"])
        self.assertFalse(gate.verify(needs, plan, current + current)["ok"])

    def test_paginated_exact_attempt_api_and_incomplete_response(self):
        calls = []
        def request(url, token):
            calls.append(url)
            self.assertEqual(token, "test-token")
            if url.endswith("&page=1"):
                return {"total_count": 101, "jobs": [{"id": i, "name": str(i), "run_attempt": 2} for i in range(1, 101)]}
            return {"total_count": 101, "jobs": [{"id": 101, "name": "last", "run_attempt": 2}]}
        jobs = gate.fetch_jobs("example/project", 44, 2, "test-token", request)
        self.assertEqual(len(jobs), 101)
        self.assertEqual(len(calls), 2)
        self.assertTrue(all("/runs/44/attempts/2/jobs?" in url for url in calls))
        def incomplete(url, token):
            return {"total_count": 2, "jobs": [{"id": 1, "name": "first"}]} if url.endswith("&page=1") else {"total_count": 2, "jobs": []}
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            gate.fetch_jobs("example/project", 44, 2, "test-token", incomplete)
        data = {"total_count": 2, "jobs": [{"id": 1, "name": "old", "run_attempt": 1}, {"id": 2, "name": "new", "run_attempt": 2}]}
        self.assertEqual([job["id"] for job in gate.fetch_jobs("example/project", 44, 2, "test-token", lambda *_: data)], [2])

    def test_cli_cheap_avoids_api_and_api_failure_is_red(self):
        plan, needs = plan_and_needs([])
        env = {"NEEDS_JSON": json.dumps(needs), "CI_JOB_PLAN": json.dumps(plan)}
        with patch.dict(os.environ, env, clear=True), patch.object(gate, "fetch_jobs") as fetch, redirect_stdout(io.StringIO()):
            self.assertEqual(gate.main(), 0)
            fetch.assert_not_called()
        plan, needs = plan_and_needs(["windows"])
        env = {"NEEDS_JSON": json.dumps(needs), "CI_JOB_PLAN": json.dumps(plan), "GITHUB_RUN_ATTEMPT": "2", "GITHUB_RUN_ID": "44", "GITHUB_REPOSITORY": "example/project", "GH_TOKEN": "test-token"}
        output = io.StringIO()
        with patch.dict(os.environ, env, clear=True), patch.object(gate, "fetch_jobs", side_effect=OSError("offline")) as fetch, patch.object(gate.time, "sleep"), redirect_stdout(output):
            self.assertEqual(gate.main(), 1)
            self.assertEqual(fetch.call_count, 3)
        self.assertFalse(json.loads(output.getvalue())["ok"])


if __name__ == "__main__":
    unittest.main()
