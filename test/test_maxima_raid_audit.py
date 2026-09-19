"""Censoring, occupancy weighting, and fixed-window attribution regressions."""
import unittest

from tools.maxima_raid_audit import analyze, parse


def sample(tick, **values):
    return dict(run='one', arm='fixed', observer=0, event='offense_sample',
                tick=tick, game_tick=tick, flag=3, flag_present=1, kind='raid',
                started_tick=0, target_since_tick=0, enemy=1, gid=-1, on_site=2,
                truth_ground_units=0, truth_buildings=0, truth_workers=0) | values


def counter(tick, team):
    return dict(run='one', arm='fixed', observer=0, event='offense_outcome',
                tick=tick, game_tick=tick, team=team, worker_combat_deaths=tick//128,
                warrior_combat_deaths=tick//128, melee_unit_damage=tick,
                melee_building_damage=tick*2, wheat_harvested=tick*3)


class RaidAuditTests(unittest.TestCase):
    def test_parser_keeps_truth_outcomes_and_clocks(self):
        rows = list(parse(['other output\n',
            'MAXIMA_TELEMETRY\t128\t0\toffense_sample\tkind=raid\tgame_tick=150\n']))
        self.assertEqual(rows[0]['game_tick'], 150)
        self.assertEqual(rows[0]['tick'], 128)
        self.assertEqual(rows[0]['kind'], 'raid')

    def test_arrival_empty_time_and_window_after_abandonment(self):
        rows = [sample(0, on_site=0, truth_workers=3, truth_ground_units=3),
                sample(128), sample(256, flag_present=0, kind='none'),
                sample(384, flag_present=0, kind='none')]
        rows += [counter(tick, team) for tick in (0,128,256,384) for team in (0,1)]
        summary, episodes = analyze(rows, horizons=(256,512))
        report = summary[0]
        self.assertEqual(report['active_game_ticks_estimate'], 256)
        self.assertEqual(report['arrived_empty_game_ticks_estimate'], 128)
        self.assertEqual(report['empty_on_site_warrior_ticks_estimate'], 256)
        self.assertEqual(episodes[0]['arrival'], 128)
        self.assertEqual(episodes[0]['outcomes']['256']['enemy']['worker_combat_deaths'], 2)
        self.assertIsNone(episodes[0]['outcomes']['512'])
        self.assertEqual(report['arrival_windows']['512']['censored'], 1)

    def test_building_is_a_target_and_missing_samples_are_censored(self):
        summary, episodes = analyze([sample(0, truth_buildings=1), sample(128), sample(512)])
        self.assertEqual(summary[0]['empty_game_ticks_estimate'], 0)
        self.assertEqual(summary[0]['unweighted_samples'], 2)
        self.assertEqual(episodes[0]['targets_at_arrival'], 1)

    def test_retarget_and_runs_do_not_merge_episodes(self):
        rows = [sample(0), sample(128, target_since_tick=100),
                dict(sample(0), run='two')]
        _, episodes = analyze(rows)
        self.assertEqual(len(episodes), 3)

    def test_no_observed_arrival_has_no_outcome_window(self):
        _, episodes = analyze([sample(0, on_site=0), sample(128, on_site=0)])
        self.assertIsNone(episodes[0]['arrival'])
        self.assertEqual(episodes[0]['outcomes'], {})

    def test_time_weight_uses_simulation_clock(self):
        summary, _ = analyze([sample(0, game_tick=100), sample(128, game_tick=250)])
        self.assertEqual(summary[0]['active_game_ticks_estimate'], 150)


if __name__ == '__main__':
    unittest.main()
