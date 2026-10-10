# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
import json
from pathlib import Path
import tempfile
import subprocess
import sys
from corpus import digest
from analyze import analyze
from run import CONFIGS, CANDIDATES
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
            split='development',corpus=self.cases,corpus_sha256=digest(self.cases),sources={})))
        (self.path/'complete.json').write_text('{}')
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
        for mutation in ('missing','duplicate','inexact','input','class','timing'):
            self.rows=[dict(r) for r in original]
            if mutation=='missing':self.rows.pop()
            if mutation=='duplicate':self.rows.append(self.rows[0])
            if mutation=='inexact':self.rows[0]['exact']=False
            if mutation=='input':self.rows[0]['input_sha256']='different'
            if mutation=='class':self.rows[0]['class_match']=True
            if mutation=='timing':self.rows[0]['total_ns']=-1
            self.write()
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):analyze(self.path)
        self.rows=original;self.write();(self.path/'complete.json').unlink()
        with self.assertRaises(ValueError):analyze(self.path)


if __name__=='__main__':
    unittest.main()
