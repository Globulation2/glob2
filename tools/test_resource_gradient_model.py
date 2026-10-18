# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest
from tools import resource_gradient_model as m

class ResourceGradientModelTest(unittest.TestCase):
    def setUp(self):self.model=json.loads(m.DEFAULT_MODEL.read_text())

    def test_generated_header_and_numeric_bounds(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'model.h';m.emit(self.model,p)
            self.assertEqual(p.read_text(),m.HEADER.read_text())
        broken=json.loads(json.dumps(self.model));broken['positive_mean']['coef'][0]=1<<63
        with self.assertRaises(ValueError):m.validate_model(broken)
        self.assertEqual(m.log_integer(-1),m.log_integer(0))
        self.assertEqual(m.log_integer(10**12),m.log_integer(m.LIMIT))

    @unittest.skipUnless(shutil.which('c++'),'C++ compiler required for integer parity')
    def test_exported_cpp_matches_integer_python_with_undefined_behavior_checks(self):
        rng=random.Random(2432)
        vectors=[[0]*7,[m.LIMIT]*7,[-1]*7,[m.LIMIT+1]*7]
        vectors += [[rng.randrange(-1,m.LIMIT+2) for _ in range(7)] for _ in range(4096)]
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);m.emit(self.model,p/'model.h')
            (p/'main.cpp').write_text('''#include "model.h"
#include <iostream>
int main(){std::array<ResourceGradientModel::I,7>x;while(std::cin>>x[0]){for(int i=1;i<7;++i)std::cin>>x[i];std::cout<<ResourceGradientModel::predict(x)<<'\\n';}}
''')
            subprocess.run(['c++','-std=c++20','-O2','-fsanitize=undefined','-fno-sanitize-recover=undefined',str(p/'main.cpp'),'-o',str(p/'run')],check=True)
            got=subprocess.check_output([str(p/'run')],input=''.join(' '.join(map(str,x))+'\n' for x in vectors),text=True)
            self.assertEqual([int(v) for v in got.splitlines()],[m.predict(self.model,x) for x in vectors])

    def test_manifest_integrity_complete_filter_and_deduplication(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);episode=p/'episodes.csv'
            episode.write_text('complete,queries,defer_extra,refresh_extra,'+','.join(m.FEATURES)+'\n'+
                               '1,2,10,3,'+','.join(['1']*7)+'\n'+
                               '0,1,99,0,'+','.join(['1']*7)+'\n')
            record=dict(group='state',format='duel',binary_sha256='binary',episodes='episodes.csv',episode_sha256=m.sha(episode))
            m.save(p/'manifest.json',[record,record]);rows=m.dataset([p/'manifest.json'])
            self.assertEqual(len(rows),1);self.assertEqual(rows[0]['defer_extra']-rows[0]['refresh_extra'],7)
            episode.write_text('modified')
            with self.assertRaises(ValueError):m.dataset([p/'manifest.json'])

    def test_dataset_groups_old_specs_and_new_manifests_by_the_same_map(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);episode=p/'episodes.csv'
            episode.write_text('complete,queries,defer_extra,refresh_extra,'+','.join(m.FEATURES)+'\n'+
                               '1,2,10,3,'+','.join(['1']*7)+'\n')
            spec=dict(generator='river',size=128,players=4,map_seed=1,game_seed=2)
            record=dict(group='original',format='4ffa',binary_sha256='binary',spec=spec,
                        episodes='episodes.csv',episode_sha256=m.sha(episode))
            repeat=dict(record,group='new-roster',map_group=m.map_identity(spec),spec=dict(spec,game_seed=3))
            m.save(p/'manifest.json',[record,repeat])
            rows=m.dataset([p/'manifest.json'])
            self.assertEqual(len(rows),2)
            self.assertEqual({r['group'] for r in rows},{m.map_identity(spec)})

    def test_fit_rejects_a_planned_format_with_no_complete_episodes(self):
        import argparse
        from unittest import mock
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);m.save(p/'manifest.json',[dict(format='duel'),dict(format='ffa')])
            rows=[dict(group=str(i),format='duel') for i in range(4)]
            with mock.patch.object(m,'dataset',return_value=rows), self.assertRaisesRegex(ValueError,"'ffa': 0"):
                m.fit(argparse.Namespace(manifest=[p/'manifest.json'],output=p/'model.json',header=None))

    def test_map_splits_cover_formats_even_when_maps_are_shared_across_formats(self):
        try:import sklearn
        except ImportError:self.skipTest('scikit-learn required')
        for overlap in [0,4]:
            rows=[dict(group=f'map-{i}',format='ffa') for i in range(8)]
            rows += [dict(group=f'map-{i}',format='teams') for i in range(overlap,8+overlap)]
            held,folds,coverage=m.split_maps(rows)
            self.assertFalse(held & folds.keys())
            self.assertEqual(held | folds.keys(),{r['group'] for r in rows})
            self.assertEqual(set(folds.values()),{0,1,2})
            self.assertTrue(all(n>=1 for n in coverage['holdout_maps'].values()))
            self.assertTrue(all(n>=3 for n in coverage['development_maps'].values()))
            for fold in coverage['folds']:
                self.assertTrue(all(n>=1 for n in fold['validation_maps'].values()))
                self.assertTrue(all(n>=2 for n in fold['training_maps'].values()))
            self.assertEqual((held,folds,coverage),m.split_maps(rows))

    def test_balancing_does_not_reward_larger_games_or_collection_repeats(self):
        try:import numpy as np
        except ImportError:self.skipTest('numpy required')
        rows=[dict(format='duel',group='one',collection='a')]*3 + [dict(format='duel',group='one',collection='b')]*9
        rows += [dict(format='duel',group='two',collection='c')]*2 + [dict(format='ffa',group='three',collection='d')]*20
        w=m.weights(rows)
        mass=lambda key,value:sum(v for r,v in zip(rows,w) if r[key]==value)
        self.assertAlmostEqual(mass('format','duel'),mass('format','ffa'))
        self.assertAlmostEqual(mass('group','one'),mass('group','two'))
        self.assertAlmostEqual(mass('collection','a'),mass('collection','b'))

    def test_constant_label_fit_is_supported(self):
        try:import sklearn
        except ImportError:self.skipTest('scikit-learn required')
        rows=[dict(format='duel',group=str(i//4),collection='one',defer_extra=0,refresh_extra=0,
                   **dict(zip(m.FEATURES,[i]*7))) for i in range(20)]
        model=m.fit_heads(rows)
        self.assertEqual(m.predict(model,[0]*7),0)
        m.validate_model(model)

    def test_runner_compresses_and_recovers_a_finished_unrecorded_game(self):
        import argparse
        import contextlib
        import io
        from unittest import mock
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);binary=p/'glob2';binary.write_text('frozen binary')
            plan=[dict(format='1v1',generator='river',size=64,players=2,alliances=[1,2],
                       map_seed=5101,game_seed=5102,ais=['nicowar','cortex'],generator_params={'repeat':4},
                       map_candidates=[dict(generator='isles',map_seed=5103)])]
            m.save(p/'plan.json',plan)
            args=argparse.Namespace(binary=str(binary),output=str(p/'results'),plan=str(p/'plan.json'),
                                    seeds=[],ticks=2048,threads=1,jobs=1)
            def execute(command,**kwargs):
                from types import SimpleNamespace
                if command[1:3]==['map','generate']:
                    self.assertIn('repeat=4',command)
                    Path(command[command.index('--output')+1]).write_text('map')
                else:
                    self.assertEqual([command[i+1] for i,v in enumerate(command) if v=='--player'],['nicowar','cortex'])
                    out=Path(command[command.index('--output-dir')+1])
                    (out/'result.json').write_text('{"status":"completed","ticks":2048}')
                    (out/'gradient-episodes.csv').write_text('complete,queries,defer_extra,refresh_extra,'+','.join(m.FEATURES)+'\n'+
                                                          '1,2,10,3,'+','.join(['1']*7)+'\n')
                return SimpleNamespace(returncode=0)
            with mock.patch.object(m.subprocess,'run',side_effect=execute),mock.patch.object(m.subprocess,'check_output',return_value='revision\n'),contextlib.redirect_stdout(io.StringIO()):m.run(args)
            manifest=p/'results/manifest.json';before=json.loads(manifest.read_text())
            self.assertTrue(before[0]['episodes'].endswith('.csv.gz'))
            self.assertEqual(json.loads((p/'results/coverage.json').read_text())['unselected_generators'],['isles'])
            self.assertEqual(before[0]['map_group'],m.map_identity(plan[0]))
            self.assertEqual(len(m.dataset([manifest])),1)
            manifest.unlink() # engine finished; coordinator had not committed it
            with mock.patch.object(m.subprocess,'run',side_effect=AssertionError('must recover')),mock.patch.object(m.subprocess,'check_output',return_value='revision\n'),contextlib.redirect_stdout(io.StringIO()):m.run(args)
            self.assertEqual(json.loads(manifest.read_text()),before)

    def test_map_candidates_record_rejections_and_do_not_retry_infrastructure_failures(self):
        from types import SimpleNamespace
        from unittest import mock
        spec=dict(format='1v1',generator='river',size=64,players=2,alliances=[1,2],
                  map_seed=1,game_seed=2,map_candidates=[dict(generator='isles',map_seed=3)])
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)
            def execute(command,**kwargs):
                report=Path(command[command.index('--report-file')+1])
                if command[3]=='river':
                    m.save(report,dict(report_type='generation_failure',generation=dict(outcome=dict(error='invalid_request'))))
                    return SimpleNamespace(returncode=2)
                return SimpleNamespace(returncode=0)
            with mock.patch.object(m.subprocess,'run',side_effect=execute):actual,attempts=m.generate_map('binary',spec,p,{})
            self.assertEqual(actual['generator'],'isles');self.assertEqual(actual['map_seed'],3)
            self.assertNotIn('map_candidates',actual)
            self.assertEqual([a['returncode'] for a in attempts],[2,0])
            self.assertEqual(json.loads((p/'selected-map.json').read_text()),actual)
            with mock.patch.object(m.subprocess,'run',return_value=SimpleNamespace(returncode=-9)) as call:
                with self.assertRaises(subprocess.CalledProcessError):m.generate_map('binary',spec,p,{})
                self.assertEqual(call.call_count,1)
            with mock.patch.object(m.subprocess,'run',side_effect=execute):
                with self.assertRaisesRegex(RuntimeError,'All map candidates rejected'):
                    m.generate_map('binary',dict(spec,map_candidates=[]),p,{})

    def test_fit_keeps_all_collection_rounds_for_a_state_out_of_training(self):
        try:import sklearn
        except ImportError:self.skipTest('scikit-learn required')
        import argparse
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);manifest=[]
            for fmt in ['duel','ffa']:
                for state in range(4):
                    for collection in range(2):
                        episode=p/f'{fmt}-{state}-{collection}.csv'
                        lines=['complete,queries,defer_extra,refresh_extra,'+','.join(m.FEATURES)]
                        for j in range(40):
                            values=[1,2,j%9,((j+2)%7),*([j+state+1]*7)]
                            lines.append(','.join(map(str,values)))
                        episode.write_text('\n'.join(lines)+'\n')
                        manifest.append(dict(group=f'{fmt}-{state}',format=fmt,binary_sha256=f'round{collection}',
                                             episodes=episode.name,episode_sha256=m.sha(episode)))
            m.save(p/'manifest.json',manifest)
            with contextlib.redirect_stdout(io.StringIO()):
                m.fit(argparse.Namespace(manifest=[p/'manifest.json'],output=p/'model.json',header=p/'model.h'))
            model=json.loads((p/'model.json').read_text());provenance=model['provenance']
            self.assertFalse(set(provenance['development_states']) & set(provenance['heldout_states']))
            self.assertEqual(provenance['holdout']['states'],2)
            self.assertEqual(len(provenance['cross_validation']),3)
            m.validate_model(model)

if __name__=='__main__':unittest.main()
