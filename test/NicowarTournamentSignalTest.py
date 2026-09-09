#!/usr/bin/env python3

from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import nicowar_tournament_signal as signal


def snapshot(tick, team, workers, warriors, buildings, attack, won=0, lost=0):
    return {
        "tick": tick, "team": team, "version": f"ai-{team}", "alive": 1,
        "won": won, "lost": lost, "population": workers + warriors,
        "workers": workers, "explorers": 0, "warriors": warriors,
        "buildings": buildings, "building_sites": 0, "food": workers * 2,
        "food_capacity": workers * 3, "total_hp": (workers + warriors) * 100,
        "attack_power": attack, "defense_power": buildings * 50, "prestige": 0,
    }


class NicowarTournamentSignalTest(unittest.TestCase):
    def match(self, steps=5000, max_steps=10000):
        return {
            "id": 1, "block": 1, "round": 1, "map": "test", "seed": 1,
            "steps": steps, "max_steps": max_steps, "status": "completed",
            "engine_status": "completed",
            "players": [
                {"team": 0, "version": "A", "won": True, "lost": False,
                 "alive": True, "units": 50, "buildings": 10, "prestige": 0},
                {"team": 1, "version": "B", "won": False, "lost": True,
                 "alive": False, "units": 10, "buildings": 2, "prestige": 0},
            ],
            "score_telemetry": [
                snapshot(1, 0, 20, 10, 5, 500), snapshot(1, 1, 10, 2, 2, 100),
                snapshot(steps, 0, 35, 15, 10, 900, won=1),
                snapshot(steps, 1, 5, 1, 1, 50, lost=1),
            ],
        }

    def test_pair_score_is_zero_sum_and_decisive_sign_is_stable(self):
        match = self.match()
        left = signal.pair_score(match, {0}, {1}, 1)
        right = signal.pair_score(match, {1}, {0}, -1)
        self.assertGreater(left["victory_score"], 55)
        self.assertAlmostEqual(left["victory_score"], -right["victory_score"], places=6)

    def test_fast_win_scores_more_than_same_slow_win(self):
        fast = signal.pair_score(self.match(steps=2500), {0}, {1}, 1)
        slow = signal.pair_score(self.match(steps=9000), {0}, {1}, 1)
        self.assertGreater(fast["victory_score"], slow["victory_score"])

    def test_draw_uses_dominance_without_decisive_bonus(self):
        score = signal.pair_score(self.match(), {0}, {1}, 0)
        self.assertGreater(score["victory_score"], 0)
        self.assertLessEqual(score["victory_score"], signal.DOMINANCE_WEIGHT)

    def test_ffa_scores_sum_to_zero(self):
        match = self.match()
        match["players"].append(
            {"team": 2, "version": "C", "won": False, "lost": True,
             "alive": False, "units": 5, "buildings": 1, "prestige": 0}
        )
        match["score_telemetry"].extend([
            snapshot(1, 2, 8, 1, 1, 40),
            snapshot(match["steps"], 2, 2, 0, 0, 0, lost=1),
        ])
        signal.enrich_ffa_match(match)
        self.assertAlmostEqual(sum(player["victory_score"] for player in match["players"]), 0.0, places=6)

    def test_old_results_receive_explicit_fallback_quality(self):
        match = self.match()
        match.pop("score_telemetry")
        signal.enrich_ffa_match(match)
        self.assertTrue(all(player["signal_quality"] == "final_state_fallback" for player in match["players"]))


if __name__ == "__main__":
    unittest.main()
