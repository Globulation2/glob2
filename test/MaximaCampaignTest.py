#!/usr/bin/env python3
"""Checks campaign pairing, frozen-parent settings, and event selection."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import maxima_campaign_bank as bank
import run_maxima_switch_ablation as ablation
import run_maxima_ablation_campaign as campaign
import harvest_maxima_checkpoints as harvest


class MaximaCampaignTest(unittest.TestCase):
    def test_every_tournament_format_is_scheduled(self):
        self.assertEqual(set(campaign.FORMATS), {"duel", "ffa3", "ffa4", "ffa5", "2v2"})
        self.assertTrue(all(len(names) == 7 for names in campaign.FFA_MAPS.values()))

    def test_ffa_rotations_cover_all_seats_and_use_correct_player_count(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'test.map'; path.touch()
            args = SimpleNamespace(maps=[str(path)], rounds=5, seed=100, players=5,
                                   all_seats=False, seat_rotations=2, format='ffa5',
                                   opponent_ai=5, max_steps=60000, interval=1000)
            rows = harvest.schedule(args)
            self.assertEqual(len(rows), 10)
            self.assertEqual({r['candidate_seat'] for r in rows}, set(range(5)))
            self.assertTrue(all(len({r['candidate_seat'] for r in rows if r['round'] == k}) == 2
                                for k in range(5)))
            command = harvest.source_command('binary', str(path), 'saves', args, rows[0])
            self.assertEqual(command[1], '-nicowar-scenario-match-nox')
            self.assertEqual(command[4], '5')

    def test_ffa_focal_team_is_not_assumed_equal_to_player(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp)/'run.log'
            log.write_text('NICOWAR_SCENARIO_PLAYER_RESULT\tcandidate\tMaxima\t2\t4\t0\t0\t1\n')
            source = {'log': str(log), 'format': 'ffa3', 'candidate_seat': 2, 'round': 0,
                      'map': 'Island_of_the_Renfur.map', 'name': 'run', 'seed': 7}
            meta, _, _ = bank.contexts(source, 'bank')
            self.assertEqual(meta['focal_player'], 2)
            self.assertEqual(meta['focal_team'], 4)
            self.assertEqual(meta['allied_teams'], [4])

    def test_controller_adopts_existing_stage_without_launching_another(self):
        with tempfile.TemporaryDirectory() as temp:
            control = object.__new__(campaign.Campaign)
            control.output = Path(temp)
            command = ['python', '-u', '/frozen/runner.py', '/results/manifest.json']
            control.state = {'completed_stages': [], 'child_pid': 123}
            control.adoption = {'stage': 'wave1-duel-from-start', 'command': command, 'pid': 123}
            control.verify = lambda: None
            control.save = lambda **values: control.state.update(values)
            observed = [SimpleNamespace(stdout='S python -u /frozen/runner.py /results/manifest.json'),
                        SimpleNamespace(stdout='')]
            with patch.object(campaign.subprocess, 'run', side_effect=observed), \
                 patch.object(campaign.subprocess, 'Popen') as launch, \
                 patch.object(campaign.time, 'sleep'):
                control.command('wave1-duel-from-start', command)
            launch.assert_not_called()
            self.assertIsNone(control.state['child_pid'])

    def test_child_arm_changes_only_the_child(self):
        command = ablation.command_for("binary", "save", {
            "switch": "farming.wood_firebreak_enabled", "focal_player": 2}, False, 20000)
        self.assertEqual(command[-2:], ["2", "farming.wood_firebreak_enabled=false"])
        self.assertNotIn("farming.enabled=false", ",".join(command))

    def test_allied_survivor_win_is_a_team_win(self):
        rows = [{"tick": 20, "team": t, "won": int(t == 2), "lost": int(t != 2)}
                for t in range(4)]
        with patch.object(ablation.scoring, "pair_score", return_value={}) as score:
            ablation.rollout_score(rows, 0, 30, [0, 2])
        self.assertEqual(score.call_args.args[1:], ({0, 2}, {1, 3}, 1))

    def test_farming_events_require_actual_evidence(self):
        self.assertNotIn("farming.coastal_porosity_enabled",
                         bank.event_switches("farming_policy", {"porous_components": "0"}))
        self.assertIn("farming.coastal_porosity_enabled",
                      bank.event_switches("farming_policy", {"porous_components": "1"}))
        self.assertIn("farming.wheat_invasion_clearing_enabled",
                      bank.event_switches("maintenance_clearing", {"wheat_invasion_wood": "3"}))

    def test_pre_context_save_and_source_block_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            save0, save1 = directory / "zero.game", directory / "later.game"
            save0.touch(); save1.touch()
            log = directory / "run.log"
            log.write_text(
                f"MAXIMA_CHECKPOINT_SAVED\tpath={save0}\ttick=0\tchecksum=a\trng=b\n"
                f"MAXIMA_CHECKPOINT_SAVED\tpath={save1}\ttick=1000\tchecksum=c\trng=d\n"
                "MAXIMA_TELEMETRY\t1001\t0\tmaintenance_clearing\twheat_invasion_wood=4\n"
                "NICOWAR_SCENARIO_PLAYER_RESULT\tcandidate\tMaxima\t0\t0\t0\t0\t1\n")
            source = {"log": str(log), "name": "run-00001", "map": "maps/G2.map",
                      "seed": 42, "round": 0, "candidate_seat": 0, "format": "duel", "error": ""}
            (directory / "source-runs.json").write_text(json.dumps([source]))
            rows = bank.build([directory], ["farming.wheat_invasion_clearing_enabled"],
                              "context", 20000)
            self.assertEqual(rows[0]["checkpoint_tick"], 0)
            self.assertEqual(rows[0]["path"], str(save0))
            meta, _, _ = bank.contexts(source, "first-bank")
            other, _, _ = bank.contexts(source, "second-bank")
            self.assertEqual(meta["source_block"], other["source_block"])
            self.assertNotEqual(meta["source_id"], other["source_id"])

    def test_2v2_partition_preserves_the_other_ally(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp)/"run.log"; log.write_text("")
            source = {"log": str(log), "format": "2v2", "partition": 1, "swap": 1,
                      "round": 1, "map": "G2.map", "name": "run", "seed": 7}
            meta, _, _ = bank.contexts(source, "bank")
            self.assertEqual(meta["allied_teams"], [1, 3])
            self.assertEqual(meta["focal_player"], 3)


if __name__ == "__main__":
    unittest.main()
