"""Partial CI selection preserves four-way partitions and full-only auditing."""
import json
from pathlib import Path
import re
import sys
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = (ROOT / ".github/workflows/ci-linux-build.yml").read_text()
sys.path.insert(0, str(ROOT / "test"))
import run_tests  # noqa: E402
import ci_native_shard_plan  # noqa: E402


def block(name):
    match = re.search(r"^  " + re.escape(name) + r":\n(.*?)(?=^  [a-zA-Z_-]+:|\Z)", WORKFLOW, re.M | re.S)
    if not match:
        raise AssertionError(f"Missing workflow job {name}")
    return match.group(1)


def expression(text, key):
    match = re.search(r"^\s+" + re.escape(key) + r": \$\{\{ (.*?) \}\}$", text, re.M)
    if not match:
        raise AssertionError(f"Missing expression {key}")
    return match.group(1)


def evaluate(source, **overrides):
    """Evaluate this workflow's small boolean/fromJSON expression subset."""
    inputs = dict(reuse_build=False, generator_only=False, build_only=False,
                  run_tests=True, run_regressions=True, test_shards="[1,2,3,4]",
                  generator_suites="", map_generators=True, coverage_profile="full")
    inputs.update(overrides)
    translated = source.replace("&&", " and ").replace("||", " or ")
    translated = re.sub(r"!(?!=)", "not ", translated)
    functions = {"inputs": SimpleNamespace(**inputs), "cancelled": lambda: False,
                 "needs": SimpleNamespace(build=SimpleNamespace(result="success"), reuse=SimpleNamespace(result="skipped")),
                 "fromJSON": json.loads, "join": lambda values, separator: separator.join(str(v) for v in values)}
    return eval(translated, {"__builtins__": {}}, functions)


class PartialLinuxWorkflowTest(unittest.TestCase):
    def test_omitted_inputs_keep_existing_full_coverage(self):
        for name, expected in (("run_regressions", "true"), ("test_shards", "'[1,2,3,4]'"), ("generator_suites", "''")):
            self.assertRegex(WORKFLOW, r"(?m)^      " + name + r":\n        type: (?:boolean|string)\n        default: " + re.escape(expected) + r"$")
        self.assertTrue(evaluate(expression(block("regressions"), "if")))
        self.assertEqual(evaluate(expression(block("tests"), "shard")), [1, 2, 3, 4])
        self.assertTrue(evaluate(expression(block("audit"), "if")))
        self.assertEqual(evaluate(expression(block("generators"), "suite")), ["sweep-a", "sweep-b", "telemetry"])
        self.assertEqual(evaluate(expression(block("generators"), "suite"), coverage_profile="compatibility"), ["golden", "telemetry"])

    def test_partial_native_shards_skip_full_coverage_auditor(self):
        for shards in ("[1]", "[2,4]", "[1, 2, 3]", "[]", "[ ]"):
            with self.subTest(shards=shards):
                self.assertFalse(evaluate(expression(block("audit"), "if"), test_shards=shards))
        self.assertTrue(evaluate(expression(block("audit"), "if"), test_shards="[1, 2, 3, 4]"))
        self.assertIn("--shards 4", block("audit"))
        self.assertNotIn("continue-on-error", block("audit"))

    def test_empty_matrices_are_guarded_before_expansion(self):
        for empty in ("[]", "[ ]"):
            with self.subTest(empty=empty):
                self.assertFalse(evaluate(expression(block("tests"), "if"), test_shards=empty))
                self.assertFalse(evaluate(expression(block("generators"), "if"), generator_suites=empty))
        self.assertTrue(evaluate(expression(block("tests"), "if"), test_shards="[2]"))
        self.assertTrue(evaluate(expression(block("generators"), "if"), generator_suites='["telemetry"]'))

    def test_explicit_generator_suites_override_both_profiles(self):
        for profile in ("full", "compatibility"):
            self.assertEqual(evaluate(expression(block("generators"), "suite"), coverage_profile=profile, generator_suites='["sweep-b"]'), ["sweep-b"])
            self.assertEqual(evaluate(expression(block("generators"), "suite"), coverage_profile=profile, generator_suites='["telemetry"]'), ["telemetry"])

    def test_optional_regressions_do_not_override_build_guards(self):
        condition = expression(block("regressions"), "if")
        self.assertFalse(evaluate(condition, run_regressions=False))
        self.assertFalse(evaluate(condition, generator_only=True))
        self.assertFalse(evaluate(condition, build_only=True))
        self.assertFalse(evaluate(expression(block("tests"), "if"), run_tests=False))
        self.assertFalse(evaluate(expression(block("generators"), "if"), map_generators=False))

    def test_selected_shards_retain_original_four_way_case_and_auxiliary_assignment(self):
        test_job = block("tests")
        # Both the planner's auxiliary placement and engine case placement retain
        # the full partition count, even when only two runners are requested.
        planner = next(line for line in test_job.splitlines() if "run: python3 test/ci_native_shard_plan.py" in line)
        count = int(re.search(r"--count (\d+)\b", planner).group(1))
        self.assertEqual(count, 4)
        self.assertIn("--shard ${{ matrix.shard }}/4", test_job)
        items = [SimpleNamespace(label=f"case-{i:02d}") for i in range(24)]
        auxiliary = ["aux:unit", "aux:save-continuation"]
        timings = {item.label: float(i + 1) for i, item in enumerate(items)}
        timings.update({"aux:unit": 5.5, "aux:save-continuation": 15.5})
        full = {k: run_tests.shard(items, f"{k}/{count}", timings, auxiliary) for k in range(1, count + 1)}
        chosen = evaluate(expression(test_job, "shard"), test_shards="[2,4]")
        partial = {k: run_tests.shard(items, f"{k}/{count}", timings, auxiliary) for k in chosen}
        self.assertEqual({k: [x.label for x in v] for k, v in partial.items()}, {k: [x.label for x in full[k]] for k in chosen})
        auxiliary_jobs = {name: {"id": name.removeprefix("aux:"), "default_shard": 1} for name in auxiliary}
        full_auxiliary = ci_native_shard_plan.plan(items, auxiliary_jobs, count, timings)
        partial_auxiliary = {name: shard for name, shard in full_auxiliary.items() if shard in chosen}
        self.assertEqual(partial_auxiliary, {name: shard for name, shard in ci_native_shard_plan.plan(items, auxiliary_jobs, 4, timings).items() if shard in chosen})
        actual = {x.label for v in partial.values() for x in v}
        omitted = {x.label for k, v in full.items() if k not in chosen for x in v}
        self.assertTrue(actual)
        self.assertTrue(omitted)
        self.assertFalse(actual & omitted)
        self.assertEqual(actual | omitted, {x.label for x in items})
        # A two-way rebalance would change the selected inventory; prevent that
        # tempting optimization from silently changing the meaning of shard IDs.
        rebalanced = {x.label for x in run_tests.shard(items, "2/2", timings, auxiliary)}
        self.assertNotEqual({x.label for x in partial[2]}, rebalanced)


if __name__ == "__main__":
    unittest.main()
