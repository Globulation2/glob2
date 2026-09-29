#!/usr/bin/env python3
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.map_telemetry import summarize

class Telemetry(unittest.TestCase):
    def record(self, name, values, category='success'):
        return {'job': {'id': name, 'type': 'generate_map', 'build': 'build', 'seeds': {'map':1},
            'config': {'generator': 'canals', 'params': {'width':7}}, 'labels': {}},
            'category': category, 'result': {'map_report': {'schema_version':2,
                'quality': {'colonies':[{'room':4}, {'room':None}]},
                'generation': {'revision':3, 'seed':1, 'telemetry': {'schema_version':1,
                    'enabled':True, 'records': values, 'dropped_records':0, 'invalid_values':0}}}}}
    def test_map_weighting_and_lossless_records(self):
        def metric(value, kind='measurement'):
            return {'key':'canals.room', 'kind':kind, 'subject':0, 'value':value}
        a=self.record('a',[metric(1),metric(3),metric('short','fallback'),metric('short','fallback'),metric(True)])
        b=self.record('b',[metric(8)])
        result=summarize([a,b],100,7)
        group=result['groups'][0]
        self.assertEqual(group['metrics']['telemetry/canals.room']['mean'],5)
        self.assertEqual(group['fallbacks']['canals.room']['observed_rate'],.5)
        self.assertEqual(len(result['records']),6)
        self.assertIs(result['records'][4]['value'],True)
        self.assertEqual(result['records'][1]['sequence'],1)
        self.assertEqual(result['maps'][0]['final_metrics'],{'/schema_version':2,'/quality/colonies/0/room':4})
        self.assertEqual(result,summarize([a,b],100,7))
    def test_failures_missing_truncated_and_cohorts(self):
        a=self.record('a',[], 'generation_failure')
        a['result']['map_report']['generation']['telemetry']['dropped_records']=2
        b=copy.deepcopy(a);b['job']['id']='b';b['result']={}
        c=copy.deepcopy(a);c['job']['id']='c';c['job']['config']['params']['width']=8
        result=summarize([a,b,c],0)
        self.assertEqual(len(result['groups']),3)
        self.assertEqual(sum(g['incomplete'] for g in result['groups']),2)
        self.assertEqual(sum(g['missing'] for g in result['groups']),1)
        self.assertFalse(any(g['metrics'] for g in result['groups']))

if __name__=='__main__': unittest.main()
