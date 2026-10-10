# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
import json
from pathlib import Path
import tempfile
import subprocess
import sys
from unittest.mock import mock_open, patch
from corpus import digest
from analyze import analyze
from contracts import CONFIGS, CANDIDATES, sources, finish_run, consume_holdouts, validate_completed, sha
from corpus import manifest, fields, qualifying_class, DEVELOPMENT_FAMILIES, HELD_OUT_FAMILIES
from analyze import wins


class CorpusTests(unittest.TestCase):
    def test_independence_and_reserved_topologies(self):
        dev, final = manifest('development'), manifest('final')
        self.assertEqual(len(final),1000)
        self.assertEqual(len({x['seed'] for x in final+dev}),len(final+dev))
        for size in (64,128,256,512,1024):
            self.assertEqual(sum(x['width']==size for x in final),200)
        self.assertEqual({x['family'] for x in dev},set(DEVELOPMENT_FAMILIES))
        self.assertTrue(set(HELD_OUT_FAMILIES).isdisjoint(x['family'] for x in dev))
        self.assertTrue(set(HELD_OUT_FAMILIES).issubset(x['family'] for x in final))

    def test_deterministic_chronology_and_awkward_inputs(self):
        import numpy as np
        for w,h in ((1,1),(1,17),(23,1),(7,13),(63,65)):
            for family in (*DEVELOPMENT_FAMILIES,*HELD_OUT_FAMILIES):
                # Test-only seeds; do not generate final layout inputs.
                case=dict(width=w,height=h,seed=17,family=family)
                a,b=list(fields(case)),list(fields(case))
                self.assertEqual(len(a),3)
                for x,y in zip(a,b):
                    np.testing.assert_array_equal(x['seeds'],y['seeds'])
                    np.testing.assert_array_equal(x['costs'],y['costs'])
                    self.assertEqual(x['seeds'].size,w*h)
                    self.assertTrue(np.all(x['costs']&65535>0))
                    self.assertEqual(x['seeds'][0],65535)

    def test_classes_use_declared_metadata(self):
        base=dict(width=256,height=256,cap=40,seed_count=65)
        self.assertTrue(qualifying_class('global',base))
        self.assertTrue(qualifying_class('frontier',base))
        self.assertFalse(qualifying_class('global',dict(base,cap=41)))
        self.assertFalse(qualifying_class('frontier',dict(base,seed_count=66)))


class AdmissionTests(unittest.TestCase):
    def test_failed_final_setup_does_not_consume_or_generate_holdouts(self):
        import run

        # Admission is already satisfied. Exercise only preflight/setup failures;
        # no native compilation, GPU initialization or final fields are permitted.
        for failure in ('existing-output', 'compilation', 'device'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                reports = {}
                provenance = sources()
                for split in ('development', 'stress'):
                    path = root / split / 'analysis.json'
                    path.parent.mkdir()
                    reports[split] = dict(split=split, sources=provenance,
                                          screen_survivors=['global'])
                    path.write_text(json.dumps(reports[split]))
                edge = root / 'edges.json'
                edge.write_text(json.dumps(dict(exact=True, cases=120,
                                                executions=1039, sources=provenance)))
                output = root / 'final'
                if failure == 'existing-output':
                    output.mkdir()
                argv = ['run.py', '--split', 'final', '--output', str(output),
                        '--development-report', str(root / 'development' / 'analysis.json'),
                        '--stress-report', str(root / 'stress' / 'analysis.json'),
                        '--edge-report', str(edge)]
                with patch.object(sys, 'argv', argv), \
                        patch('analyze.analyze', side_effect=lambda path: reports[path.name]), \
                        patch('run.ROOT', root), patch('run.open', mock_open()), \
                        patch('run.fcntl.flock'), \
                        patch('run.compile_native', side_effect=RuntimeError('compile failed')
                              if failure == 'compilation' else None, return_value=[]), \
                        patch('run.Runner', side_effect=RuntimeError('device unavailable')), \
                        patch('run.consume_holdouts') as consume, \
                        patch('run.fields') as generate:
                    with self.assertRaises(FileExistsError if failure == 'existing-output' else RuntimeError):
                        run.main()
                    consume.assert_not_called()
                    generate.assert_not_called()
                self.assertFalse((root / 'artifacts').exists())

    def test_final_requires_evidence_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'final'
            result=subprocess.run([sys.executable,str(Path(__file__).parent/'run.py'),
                '--split','final','--output',str(output)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('requires complete',result.stderr)
            self.assertFalse(output.exists())

    def test_noise_and_cold_regression_do_not_become_wins(self):
        protocol=dict(noise_multiplier_mad=3,minimum_absolute_advantage_ns=10000,
                      minimum_relative_advantage=.05)
        def rows(values):
            return [dict(repeat=i-1,total_ns=v) for i,v in enumerate(values)]
        baseline=rows([100000,100000,100000,100000])
        self.assertTrue(wins(rows([70000]*4),baseline,protocol)['win'])
        self.assertFalse(wins(rows([110000,70000,70000,70000]),baseline,protocol)['win'])
        self.assertFalse(wins(rows([70000,30000,70000,120000]),baseline,protocol)['win'])
        self.assertFalse(wins(rows([99000]*4),baseline,protocol)['win'])


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)
        protocol=json.loads((Path(__file__).parent/'protocol.json').read_text())
        self.cases=manifest('development')
        (self.path/'freeze.json').write_text(json.dumps(dict(protocol=protocol,
            split='development',corpus=self.cases,corpus_sha256=digest(self.cases),sources=sources(),candidates=list(CANDIDATES))))
        self.rows=[]
        for layout in self.cases:
            for stage in range(3):
                for plan in (*CONFIGS,*CANDIDATES):
                    for repeat in range(-1,protocol['warm_repetitions']):
                        metadata=dict(width=layout['width'],height=layout['height'],cap=10,
                                      seed_count=1,minimum_step=10)
                        self.rows.append(dict(layout=layout,stage=stage,plan=plan,repeat=repeat,
                            **{k:v for k,v in metadata.items() if k not in ('width','height')},
                            cost_classes=1,obstacle_count=0,input_sha256='same-field',
                            class_match=qualifying_class(plan,metadata) if plan in CANDIDATES else False,
                            eligible=True,exact=True,total_ns=100000 if plan in CONFIGS else 50000))

    def write(self):
        (self.path/'results.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in self.rows))
        finish_run(self.path,json.loads((self.path/'freeze.json').read_text()),len(self.rows),
                   sum(r.get('eligible',True) for r in self.rows))

    def test_repetitions_and_chronology_never_inflate_map_count(self):
        self.write();result=analyze(self.path)
        self.assertEqual(result['summary']['global']['independent_class_maps'],24)
        self.assertEqual(result['summary']['global']['winning_maps'],24)
        self.assertEqual(result['admitted'],[])
        self.assertEqual(result['kernel_qualified'],[])
        # One losing chronological field defeats that whole map.
        target=next(c['id'] for c in self.cases if c['width']==256)
        for row in self.rows:
            if row['layout']['id']==target and row['stage']==1 and row['plan']=='global':
                row['total_ns']=150000
        self.write()
        self.assertEqual(analyze(self.path)['summary']['global']['winning_maps'],23)

    def test_incomplete_duplicate_inexact_and_mismatched_inputs_fail_closed(self):
        original=list(self.rows)
        for mutation in ('missing','duplicate','inexact','input','class','timing','truthy','bool-time'):
            self.rows=[dict(r) for r in original]
            if mutation=='missing':self.rows.pop()
            if mutation=='duplicate':self.rows.append(self.rows[0])
            if mutation=='inexact':self.rows[0]['exact']=False
            if mutation=='input':self.rows[0]['input_sha256']='different'
            if mutation=='class':self.rows[0]['class_match']=True
            if mutation=='timing':self.rows[0]['total_ns']=-1
            if mutation=='truthy':self.rows[0]['exact']='false'
            if mutation=='bool-time':self.rows[0]['total_ns']=True
            self.write()
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):analyze(self.path)
        self.rows=original;self.write();(self.path/'complete.json').unlink()
        with self.assertRaises(ValueError):analyze(self.path)

    def test_completion_protocol_source_and_results_are_bound(self):
        self.write()
        saved={name:(self.path/name).read_bytes() for name in ('freeze.json','complete.json','results.jsonl')}
        for changed in ('protocol','source','result','count','empty-receipt'):
            for name,data in saved.items():(self.path/name).write_bytes(data)
            freeze=json.loads(saved['freeze.json']); receipt=json.loads(saved['complete.json'])
            if changed=='protocol':
                freeze['protocol']['minimum_win_fraction']=0
                (self.path/'freeze.json').write_text(json.dumps(freeze))
                receipt['freeze_sha256']=sha(self.path/'freeze.json')
            if changed=='source':
                freeze['sources']={}
                (self.path/'freeze.json').write_text(json.dumps(freeze))
                receipt['freeze_sha256']=sha(self.path/'freeze.json')
            if changed=='result':
                with (self.path/'results.jsonl').open('a') as stream:stream.write('\n')
            if changed=='count':receipt['records']=1
            if changed=='empty-receipt':receipt={}
            (self.path/'complete.json').write_text(json.dumps(receipt))
            with self.subTest(changed=changed),self.assertRaises(ValueError):analyze(self.path)

    def test_final_qualifies_only_frozen_development_survivors(self):
        # Contract-only final fixture uses a small mocked roster; never generates
        # or consumes real held-out inputs. Other candidates win these synthetic
        # records too, but cannot be qualified without development admission.
        freeze=json.loads((self.path/'freeze.json').read_text())
        freeze['split']='final';freeze['candidates']=['global']
        freeze['protocol'].update(minimum_held_out_class_maps=1,final_layouts=40,layouts_per_square_size=8)
        prior=dict(split='development',sources=sources(),screen_survivors=['global'])
        path=self.path/'development-analysis.json';path.write_text(json.dumps(prior))
        freeze['development_report_sha256']=sha(path)
        (self.path/'freeze.json').write_text(json.dumps(freeze));self.write()
        with patch('contracts.manifest',return_value=self.cases), patch('contracts.load_protocol',return_value=freeze['protocol']):
            result=analyze(self.path)
        self.assertEqual(result['eligible_candidates'],['global'])
        self.assertEqual(result['kernel_qualified'],['global'])
        self.assertNotIn('frontier',result['screen_survivors'])
        freeze['candidates']=['global','frontier']
        (self.path/'freeze.json').write_text(json.dumps(freeze));self.write()
        with patch('contracts.manifest',return_value=self.cases), patch('contracts.load_protocol',return_value=freeze['protocol']), self.assertRaises(ValueError):
            analyze(self.path)

    def test_holdout_receipt_is_one_use(self):
        ledger=self.path/'ledger'
        consume_holdouts(ledger,self.cases,self.path)
        with self.assertRaises(FileExistsError):consume_holdouts(ledger,self.cases,self.path)


if __name__=='__main__':
    unittest.main()
