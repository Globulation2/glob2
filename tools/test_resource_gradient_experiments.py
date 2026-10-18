# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from tools import resource_gradient_experiments as e


class ExperimentsTest(unittest.TestCase):
    def test_interval_contract(self):
        self.assertEqual(e.intervals('0,32,128,512'), [0,32,128,512])
        for value in ['', '32,32', '1,32','-1','65537','x']:
            with self.subTest(value=value), self.assertRaises(ValueError): e.intervals(value)

    def test_per_game_seeds_are_reproducible_and_vary_field_assignments(self):
        seeds=[e.exploration_seed_for_game(181022,str(i)) for i in range(256)]
        self.assertEqual(seeds,[e.exploration_seed_for_game(181022,str(i)) for i in range(256)])
        self.assertEqual(len(set(seeds)),256)
        self.assertTrue(all(0 <= seed <= 2**32-1 for seed in seeds))
        self.assertNotEqual(seeds[0],e.exploration_seed_for_game(181023,"0"))

    def test_immutable_config(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)
            e._frozen(p,dict(seed=1));e._frozen(p,dict(seed=1))
            with self.assertRaises(ValueError):e._frozen(p,dict(seed=2))

    def test_completed_result_cannot_acquire_new_provenance(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'result.json').write_text('{"status":"completed"}')
            with self.assertRaisesRegex(ValueError,'no command provenance'):
                e._execute(['binary','game','run'],p,60)
            self.assertFalse((p/'command.json').exists())
            with self.assertRaisesRegex(ValueError,'no frozen provenance'):
                e._frozen(p,dict(seed=1))

    def test_checkpoint_branches_freeze_state_and_distinct_waits(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'binary').write_text('binary');(p/'save').write_text('save')
            args=argparse.Namespace(binary=p/'binary',checkpoint=p/'save',output=p/'out',
                                    field=42,wait=[32,32,128],ticks=1024,threads=1,timeout_seconds=60)
            calls=[]
            def execute(command,directory,timeout):
                calls.append(command);directory.mkdir(parents=True)
                (directory/'result.json').write_text('{"status":"completed"}')
                (directory/'gradient-decisions.csv').write_text('tick,key,reason,admitted\n512,42,checkpoint_fork,1\n')
                return dict(status='completed')
            with mock.patch.object(e,'_execute',side_effect=execute),mock.patch.object(e,'_quality',return_value={}),contextlib.redirect_stdout(io.StringIO()):e.branch(args)
            self.assertEqual([c[c.index('--resource-gradient-fork-wait')+1] for c in calls],['0','32','128'])
            self.assertTrue(all(c[c.index('--load-game')+1]==str((p/'save').resolve()) for c in calls))
            self.assertEqual(len(json.loads((p/'out/report.json').read_text())['arms']),3)

    def test_compare_interleaves_and_keeps_oracle_out_of_cpu_runs(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)
            for name in ['incumbent','candidate','save']: (p/name).write_text(name)
            args=argparse.Namespace(incumbent=p/'incumbent',candidate=p/'candidate',checkpoint=[p/'save'],
                                    output=p/'out',ticks=1024,warmup=256,threads=1,repeats=2,
                                    timeout_seconds=60,quality=True)
            calls=[]
            def execute(command,directory,timeout):
                calls.append(command);directory.mkdir(parents=True)
                result=dict(status='completed',ticks=1024,benchmark_measured_ticks=768,
                            benchmark_run_cpu_ns=768000,run_ns=1024000,finalChecksum=123)
                (directory/'result.json').write_text(json.dumps(result))
                return result
            with mock.patch.object(e,'_execute',side_effect=execute),mock.patch.object(e,'_quality',return_value={}),contextlib.redirect_stdout(io.StringIO()):e.compare(args)
            self.assertEqual([Path(c[0]).name for c in calls[:4]],['incumbent','candidate','candidate','incumbent'])
            self.assertTrue(all('--resource-gradient-probe' not in c for c in calls[:4]))
            self.assertTrue(all('--benchmark-warmup' not in c for c in calls[4:]))
            report=json.loads((p/'out/report.json').read_text())
            self.assertEqual(report['median_paired_cpu_per_tick_ratio'],1)
            self.assertEqual(len(report['quality']),2)

    def test_quality_missing_schema_is_not_silently_zero(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'gradient-quality.csv').write_text('tick,flagged\n1,0\n')
            with self.assertRaisesRegex(ValueError,'schema missing'):e._quality(p)

if __name__=='__main__':unittest.main()
