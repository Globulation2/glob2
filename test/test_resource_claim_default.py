#!/usr/bin/env python3
"""Tournament jobs choose only the shipped claim policy or claims off."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.experiments import Planner
from tools.tournaments.jobs import REMOVED_CLAIM_FIELDS, EngineJob

NEW = {'directory': '/bundle', 'executable': 'glob2',
       'capabilities': {'resource_claims_default': 'contention-v1', 'resource_claims_switch_version': 1}}
OLD = {'directory': '/bundle', 'executable': 'glob2', 'capabilities': {}}
AIS = {'ais': [{'id': 1, 'name': 'numbi'}, {'id': 2, 'name': 'castor'}],
       'generators': [{'method': 15, 'editorOnly': False}]}


def job(**config):
    return {'type': 'game', 'config': {'players': ['cortex', 'cortex'], 'ticks': 64, **config},
            'inputs': {'map': 'frozen-map'}, 'seeds': {'game': 17}, 'outputs': {}, 'limits': {}, 'labels': {}}


def command(item, bundle):
    adapter = EngineJob('game')
    adapter.validate(item)
    return adapter.command(item, bundle, '/attempt', {'map': '/frozen.map'})


def window(args, flag):
    return [args[i + 1] for i, arg in enumerate(args) if arg == flag]


class ResourceClaimDefaultJobs(unittest.TestCase):
    def test_no_claim_keys_mean_the_pinned_engine_default(self):
        for bundle in (NEW, OLD):
            args = command(job(), bundle)
            self.assertFalse([arg for arg in args if arg.startswith('--resource-claims')])
            self.assertNotIn('--material-destinations', args)

    def test_explicit_off_is_forwarded_only_to_engines_with_the_switch(self):
        self.assertEqual(window(command(job(resource_claims='off'), NEW), '--resource-claims'), ['off'])
        # Engines from before the default are claims-off already.
        self.assertNotIn('--resource-claims', command(job(resource_claims='off'), OLD))

    def test_explicit_default_requires_an_engine_with_one(self):
        self.assertNotIn('--resource-claims', command(job(resource_claims='default'), NEW))
        with self.assertRaises(ValueError):
            command(job(resource_claims='default'), OLD)

    def test_invalid_and_removed_setups_are_rejected(self):
        adapter = EngineJob('game')
        for bad in (job(resource_claims='on'), job(resource_claims=None)):
            with self.assertRaises(ValueError):
                adapter.validate(bad)
        for key in REMOVED_CLAIM_FIELDS:
            for item in (job(**{key: True}), job(resource_claims='off', **{key: True})):
                with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'removed resource-claim'):
                    adapter.validate(item)
        replay = job(resource_claims='off'); replay['outputs']['replay'] = True
        with self.assertRaises(ValueError):
            adapter.validate(replay)
        saved = job(resource_claims='off'); saved['inputs'] = {'save': 'frozen-save'}; saved['seeds'] = {}
        with self.assertRaises(ValueError):
            adapter.validate(saved)

    def collect(self, item, resolved):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            output.mkdir()
            (output / 'result.json').write_text(json.dumps({'schema_version': 1, 'status': 'completed', 'resolved': resolved}))
            return EngineJob('game').collect(item, directory)

    def test_results_attest_the_requested_mode(self):
        off = {'resource_claims': 'off'}
        default = {'resource_claims': 'default', 'resource_claims_policy_name': 'contention-v1'}
        with self.assertRaises(ValueError):
            self.collect(job(resource_claims='default'), off)
        with self.assertRaises(ValueError):
            self.collect(job(resource_claims='default'), {})
        with self.assertRaises(ValueError):
            self.collect(job(resource_claims='off'), default)
        self.collect(job(resource_claims='off'), off)
        self.collect(job(resource_claims='default'), default)
        # Engines before claims attest no mode and run without claims.
        self.collect(job(resource_claims='off'), {})
        self.collect(job(), {})

    def test_planners_freeze_the_claim_mode_and_reject_removed_fields(self):
        base = {'id': 'sample', 'sample_games': 3, 'sample_seed': 5}
        old = {'id': 'a' * 64, 'capabilities': AIS}
        new = {'id': 'b' * 64, 'capabilities': {**AIS, **NEW['capabilities']}}
        for bundle in (old, new):
            jobs = Planner('ai_comparison', dict(base), [bundle]).plan()['jobs']
            self.assertTrue(all('resource_claims' not in item['config'] for item in jobs))
        for mode in ('default', 'off'):
            jobs = Planner('ai_comparison', dict(base, resource_claims=mode), [new]).plan()['jobs']
            games = [item for item in jobs if item['type'] == 'game']
            self.assertTrue(games)
            self.assertTrue(all(item['config']['resource_claims'] == mode for item in games))
        with self.assertRaises(ValueError):
            Planner('ai_comparison', dict(base, resource_claims='default'), [old])
        with self.assertRaises(ValueError):
            Planner('ai_comparison', dict(base, resource_claims='on'), [new])
        for key in REMOVED_CLAIM_FIELDS:
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'removed resource-claim'):
                Planner('ai_comparison', dict(base, **{key: True}), [new])


if __name__ == '__main__':
    unittest.main()
