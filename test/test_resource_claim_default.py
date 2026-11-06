#!/usr/bin/env python3
"""Tournament jobs keep claims-off setups explicit once new games default to claims."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.jobs import EngineJob

NEW = {'directory': '/bundle', 'executable': 'glob2',
       'capabilities': {'resource_claims_default': 'contention-v1', 'resource_claims_switch_version': 1}}
OLD = {'directory': '/bundle', 'executable': 'glob2', 'capabilities': {}}


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
            self.assertNotIn('--resource-claims', args)
            self.assertNotIn('--material-destinations', args)

    def test_explicit_off_and_destination_only_setups_keep_claims_off(self):
        self.assertEqual(window(command(job(resource_claims='off'), NEW), '--resource-claims'), ['off'])
        # Before the default, material_destinations without a policy meant claims off.
        args = command(job(material_destinations=True), NEW)
        self.assertEqual(window(args, '--resource-claims'), ['off'])
        self.assertIn('--material-destinations', args)
        self.assertEqual(window(command(job(material_destinations=False), NEW), '--resource-claims'), ['off'])
        # Engines from before the default are claims-off already.
        self.assertNotIn('--resource-claims', command(job(resource_claims='off'), OLD))
        self.assertNotIn('--resource-claims', command(job(material_destinations=True), OLD))

    def test_policies_replace_the_default_without_an_off_switch(self):
        args = command(job(material_destinations=True, resource_claims_active='prototype-stationary-v1'), NEW)
        self.assertNotIn('--resource-claims', args)
        self.assertEqual(window(args, '--resource-claims-active'), ['prototype-stationary-v1'])

    def test_explicit_default_requires_an_engine_with_one(self):
        self.assertNotIn('--resource-claims', command(job(resource_claims='default'), NEW))
        with self.assertRaises(ValueError):
            command(job(resource_claims='default'), OLD)

    def test_invalid_combinations_are_rejected(self):
        adapter = EngineJob('game')
        for bad in (job(resource_claims='on'), job(resource_claims='default', material_destinations=True),
                    job(resource_claims='off', resource_claims_active='prototype-stationary-v1')):
            with self.assertRaises(ValueError):
                adapter.validate(bad)
        replay = job(resource_claims='off'); replay['outputs']['replay'] = True
        with self.assertRaises(ValueError):
            adapter.validate(replay)
        saved = job(resource_claims='off'); saved['inputs'] = {'save': 'frozen-save'}; saved['seeds'] = {}
        with self.assertRaises(ValueError):
            adapter.validate(saved)
        replay = job(material_destinations=False); replay['outputs']['replay'] = True
        adapter.validate(replay)
        with self.assertRaises(ValueError):
            adapter.command(replay, NEW, '/attempt', {'map': '/frozen.map'})

    def collect(self, item, resolved):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            output.mkdir()
            (output / 'result.json').write_text(json.dumps({'schema_version': 1, 'status': 'completed', 'resolved': resolved}))
            return EngineJob('game').collect(item, directory)

    def test_results_attest_the_requested_mode(self):
        off = {'active_claim_policy': 'disabled', 'material_destinations_enabled': False, 'resource_claims': 'off'}
        with self.assertRaises(ValueError):
            self.collect(job(resource_claims='default'), off)
        default = {'active_claim_policy': 'experimental', 'material_destinations_enabled': True,
                   'resource_claims': 'default', 'resource_claims_policy_name': 'contention-v1'}
        with self.assertRaises(ValueError):
            self.collect(job(resource_claims='off'), default)


if __name__ == '__main__':
    unittest.main()
