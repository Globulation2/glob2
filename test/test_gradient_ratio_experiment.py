"""Independent-seed planning, required evidence, and statistical unit contracts."""
import copy
import unittest
from collections import Counter, defaultdict
from tools.gradient_ratio_experiment import ARMS, NAMED, compare, plan, add_observer_calibration
from tools.tournaments.jobs import EngineJob, routing_ratio_outputs


def bundle():
    names=list(NAMED)+['isles','river','hills']
    return dict(id='a'*64,capabilities=dict(ais=[dict(name='maxima'),dict(name='castor')],generators=[
        dict(id=name,method=i,controls=[dict(id=key,values=values,searchValues=values) for key,values in
            [('width',[7,8,9]),('height',[7,8,9]),('teams',[2,3,4,6]),('workers',[4]),('water',[10,30])]]) for i,name in enumerate(names)]))

class RatioExperiment(unittest.TestCase):
    def test_plan_is_reproducible_balanced_and_independent(self):
        first=plan(bundle(),phase='pilot');self.assertEqual(first,plan(bundle(),phase='pilot'))
        self.assertEqual(len(first['jobs']),12*5*5)
        setups=first['metadata']['setups']
        self.assertEqual(sum(s['generator_id'] in NAMED for s in setups),6)
        self.assertEqual(set(s['generator_id'] for s in setups)&set(NAMED),set(NAMED))
        seeds=[(j['seeds']['map'],j['seeds']['game']) for j in first['jobs']]
        self.assertEqual(len(seeds),len(set(seeds)))
        counts=Counter((j['labels']['setup'],j['labels']['variant']) for j in first['jobs'] if not j['labels']['diagnostic'])
        self.assertEqual(set(counts.values()),{4})
        blocks=defaultdict(list)
        for j in first['jobs']:
            blocks[j['labels']['block']].append(j['labels']['variant'])
            self.assertEqual(j['config']['win_probability_permille'],950)
            self.assertNotIn('pair',j['labels'])
        self.assertGreater(len({tuple(v) for v in blocks.values()}),1)
        heldout=plan(bundle(),phase='confirmation')
        self.assertFalse({s['id'] for s in setups}&{s['id'] for s in heldout['metadata']['setups']})

    def test_calibration_is_separate_and_preserves_simulation_seeds(self):
        manifest=add_observer_calibration(plan(bundle(),phase='pilot'))
        calibration=[j for j in manifest['jobs'] if 'observer_mode' in j['labels']]
        self.assertEqual(len(calibration),24)
        groups=defaultdict(list)
        for j in calibration:
            self.assertTrue(j['labels']['diagnostic'])
            self.assertEqual(j['config']['ticks'],8192)
            groups[j['labels']['setup'],j['labels']['replicate']].append(j)
        for jobs in groups.values():
            self.assertEqual({j['labels']['observer_mode'] for j in jobs},{'off','counts','routes'})
            self.assertEqual(len({tuple(j['seeds'].values()) for j in jobs}),1)

    def test_observers_are_explicit_and_required_outputs_cannot_be_dropped(self):
        request=plan(bundle(),phase='pilot')['jobs'][0]
        adapter=EngineJob('game');adapter.validate(request)
        missing=copy.deepcopy(request);missing['outputs']['required']=[]
        with self.assertRaisesRegex(ValueError,'required outputs'):adapter.validate(missing)
        command=adapter.command(request,dict(directory='/bundle',executable='glob2'),'/attempt',{})
        self.assertIn('--gradient-work-aggregate',command)
        self.assertEqual(command[command.index('--benchmark-warmup')+1],'0')
        self.assertIn('--routing-ratio-study',command)
        self.assertEqual(command[command.index('--gpu')+1],'off')
        request['config']['routing_ratio']['policy']='unknown'
        with self.assertRaisesRegex(ValueError,'unknown routing'):adapter.validate(request)

    def test_replication_cannot_be_sacrificed_to_more_setups(self):
        with self.assertRaises(ValueError):plan(bundle(),phase='pilot',replicates=3)
        with self.assertRaises(ValueError):plan(bundle(),phase='pilot',setups=3)

    def test_setup_weight_and_game_replication_are_distinct_from_tick_counts(self):
        games=[]
        for setup in ('small','large'):
            for arm,value in [('baseline',10),('mode',8)]:
                for _ in range(4 if setup=='small' else 20):
                    games.append(dict(setup=setup,arm=arm,diagnostic=False,cpu_per_tick=value))
        result=compare(games,draws=100)['mode']
        self.assertEqual(result['effect'],-2)
        self.assertEqual(result['ci95'],[-2,-2])
        self.assertEqual(result['setups'],2)
        games.extend(dict(setup='small',arm='mode',diagnostic=True,cpu_per_tick=1000) for _ in range(100))
        self.assertEqual(compare(games,draws=100)['mode']['effect'],-2)

if __name__=='__main__':unittest.main()
