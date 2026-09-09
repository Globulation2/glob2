#!/usr/bin/env python3
"""Unit tests for the conservative Maxima checkpoint ablation harness."""

from __future__ import annotations

import sys
import unittest
import math
import re
from unittest.mock import patch
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import run_maxima_switch_ablation as ablation
import build_maxima_checkpoint_bank as bank
import harvest_maxima_checkpoints as harvest


class MaximaAblationHarnessTest(unittest.TestCase):
    def test_registry_contains_every_declared_boolean(self) -> None:
        strategy = (ROOT / "src/AIMaximaStrategy.cpp").read_text()
        declared = set(re.findall(r'BOOL_SPEC\([^\n]*?"([a-z][a-z0-9_.]+)"', strategy))
        self.assertEqual(set(ablation.ALL_SWITCHES), declared)
        self.assertEqual(len(ablation.ALL_SWITCHES), len(declared))

    def test_default_cluster_is_remote_only_and_uses_every_core(self) -> None:
        self.assertNotIn("local", {host for host, _count in ablation.REMOTE_WORKERS})
        self.assertEqual(ablation.REMOTE_WORKERS, harvest.REMOTE_WORKERS)
        self.assertEqual(sum(count for _host, count in ablation.REMOTE_WORKERS), 60)

    def test_inconclusive_always_stays_enabled(self) -> None:
        self.assertEqual(
            ablation.verdict(0.1, -1.0, 1.2, 0.5, False),
            ("inconclusive", "keep enabled"),
        )

    def test_harm_requires_confirmation_to_disable(self) -> None:
        self.assertEqual(ablation.verdict(-5, -6, -4, 3, False)[1], "keep enabled")
        self.assertEqual(ablation.verdict(-5, -6, -4, 3, True)[1], "disable")

    def test_single_block_is_always_indeterminate(self) -> None:
        center, low, high = ablation.interval([-100.0])
        self.assertEqual(center, -100.0)
        self.assertTrue(math.isinf(low) and low < 0)
        self.assertTrue(math.isinf(high) and high > 0)
        self.assertEqual(ablation.verdict(center, low, high, 3, True)[1], "keep enabled")

    def test_underpowered_harm_stays_enabled(self) -> None:
        self.assertEqual(
            ablation.verdict(-10, -12, -8, 3, True, adequately_powered=False),
            ("underpowered", "keep enabled"),
        )

    def test_harm_must_survive_multiple_comparison_control(self) -> None:
        self.assertEqual(
            ablation.verdict(-10, -12, -8, 3, True, True, False),
            ("harmful before FDR correction", "keep enabled"),
        )
        selected = ablation.benjamini_hochberg(
            {"clear": 0.001, "noise-a": 0.3, "noise-b": 0.8}, 0.05
        )
        self.assertEqual(selected, {"clear"})

    def test_same_arm_must_be_deterministic(self) -> None:
        rows = []
        for arm in ("on", "off"):
            for repetition in range(2):
                rows.append({
                    "arm": arm, "error": "", "start_tick": 100,
                    "start_checksum": "aaaa", "start_rng": "bbbb",
                    "final_tick": 200, "final_checksum": arm,
                    "final_rng": arm, "victory_score": 1 if arm == "on" else 0,
                    "repetition": repetition,
                })
        self.assertEqual(ablation.qualification(rows), (True, ""))
        rows[1]["final_checksum"] = "different"
        self.assertFalse(ablation.qualification(rows)[0])

    def test_team_score_excludes_allies_from_opponents(self) -> None:
        rows = [{"tick": 10, "team": team} for team in range(4)]
        with patch.object(ablation.scoring, "pair_score", return_value={}) as score:
            ablation.rollout_score(rows, 0, 20, [0, 2])
        self.assertEqual(score.call_args.args[1:3], ({0, 2}, {1, 3}))

    def test_one_repetition_cannot_qualify(self) -> None:
        rows = [{"arm": arm, "repetition": 0, "error": "", "start_tick": 0,
                 "start_checksum": "a", "start_rng": "b"} for arm in ("on", "off")]
        self.assertFalse(ablation.qualification(rows)[0])

    def test_checkpoint_branches_have_worker_affinity(self) -> None:
        cluster = object.__new__(ablation.AblationCluster)
        cluster._active_slots = lambda: [("host-a", 0), ("host-b", 0)]
        cluster._run_one = lambda host, slot, item, _settings: {
            "id": item["id"], "status": "completed", "host": host, "slot": slot,
        }
        schedule = []
        for checkpoint_id in ("alpha", "beta", "gamma"):
            for branch in range(4):
                schedule.append({
                    "id": len(schedule), "checkpoint": {"checkpoint_id": checkpoint_id},
                    "branch": branch,
                })
        rows = ablation.AblationCluster.run(cluster, schedule, "")
        assignments = {}
        for row in rows:
            checkpoint_id = schedule[row["id"]]["checkpoint"]["checkpoint_id"]
            assignments.setdefault(checkpoint_id, set()).add((row["host"], row["slot"]))
        self.assertTrue(all(len(slots) == 1 for slots in assignments.values()))

    def test_severity_selection_round_robins_bins(self) -> None:
        rows = [{
            "switch": "tactics.enabled", "severity": value,
            "source_block": f"source-{value}", "checkpoint_tick": value,
        } for value in range(9)]
        selected = bank.select(rows, 6)
        self.assertEqual(len(selected), 6)
        self.assertEqual({row["severity_bin"] for row in selected}, {0, 1, 2})


if __name__ == "__main__":
    unittest.main()
