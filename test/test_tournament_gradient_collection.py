#!/usr/bin/env python3
"""Frozen-input collection adapters preserve probe artifacts through generic workers."""
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.jobs import EngineJob, resource_gradient_collection_outputs
from tools.tournaments.model import job, validate_job


class GradientCollectionJobs(unittest.TestCase):
    def request(self, collection=None):
        collection = {} if collection is None else collection
        return job('game', 'a'*64, inputs={'map': {'sha256': 'b'*64}}, seeds={'game': 91},
                   config={'players': ['nicowar', 'cortex'], 'ticks': 1024,
                           'resource_gradient_collection': collection},
                   outputs={'required': list(resource_gradient_collection_outputs(collection)), 'result': 'full'})

    def command(self, request):
        return EngineJob('game').command(request, {'directory': '/bundle', 'executable': 'glob2'},
                                        '/attempt', {'map': '/immutable.map.gz'})

    def test_probe_is_explicit_and_seeded_exploration_preserves_exact_assignment(self):
        official = self.request(); validate_job(official)
        command = self.command(official)
        self.assertIn('--resource-gradient-probe', command)
        self.assertEqual(command[command.index('--gpu')+1], 'off')
        self.assertEqual(command[command.index('--compute-telemetry')+1], 'off')
        self.assertNotIn('--resource-gradient-exploration', command)
        request = self.request({'exploration': '0,32,128,512', 'seed': 4294967295})
        validate_job(request); command = self.command(request)
        self.assertEqual(command[command.index('--resource-gradient-exploration')+1], '0,32,128,512')
        self.assertEqual(command[command.index('--resource-gradient-exploration-seed')+1], '4294967295')
        self.assertNotIn('--write-replay', command)
        self.assertIn('gradient-decisions.csv', request['outputs']['required'])
        self.assertNotIn('--compute-threads', command)
        request['config']['resource_gradient_collection']['threads'] = 2
        validate_job(request); command = self.command(request)
        self.assertEqual(command[command.index('--compute-threads')+1], '2')

    def test_default_jobs_do_not_enable_or_require_probe(self):
        request = self.request(); del request['config']['resource_gradient_collection']; request['outputs'] = {}
        validate_job(request)
        self.assertNotIn('--resource-gradient-probe', self.command(request))
        self.assertNotIn('--gpu', self.command(request))
        self.assertNotIn('--compute-telemetry', self.command(request))

    def test_collection_requires_frozen_inputs_full_provenance_and_artifact_retention(self):
        for modify, message in [
            (lambda r: r['config'].update(generator='river'), 'frozen map'),
            (lambda r: r['outputs'].update(replay=True), 'replay'),
            (lambda r: r['outputs'].update(result='outcome'), 'full result'),
            (lambda r: r['outputs'].update(required=[]), 'requires output'),
        ]:
            with self.subTest(message=message):
                request = self.request(); modify(request)
                with self.assertRaisesRegex(ValueError, message): validate_job(request)
        request = self.request(); request['inputs'] = {'save': {'sha256': 'c'*64}}
        request['seeds'] = {}; del request['config']['players']; validate_job(request)

    def test_strict_collection_schema_rejects_silent_typos_and_nonportable_seeds(self):
        for collection in [False, [], {'typo': True}, {'threads': 0}, {'threads': True},
                           {'threads': 65}, {'threads': '2'}, {'seed': True}, {'seed': -1}, {'seed': 2**32},
                           {'exploration': None}, {'exploration': ''}, {'exploration': '32,'},
                           {'exploration': '32,32'}, {'exploration': '7'}, {'exploration': '65537'},
                           {'exploration': '32, 128'}, {'exploration': '+32'}]:
            with self.subTest(collection=collection):
                request = self.request(); request['config']['resource_gradient_collection'] = collection
                with self.assertRaises(ValueError): validate_job(request)

    def write_outputs(self, out, collection):
        (out/'result.json').write_text(json.dumps({'schema_version': 1, 'status': 'completed'}))
        for name, fields in resource_gradient_collection_outputs(collection).items():
            with (out/name).open('w', newline='') as stream: csv.writer(stream).writerow(sorted(fields))

    def test_completed_collection_rejects_missing_or_old_binary_outputs_but_allows_quiet_streams(self):
        request = self.request({'exploration': '0,512', 'seed': 4})
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)/'output'; out.mkdir()
            self.write_outputs(out, request['config']['resource_gradient_collection'])
            adapter = EngineJob('game')
            self.assertEqual(adapter.collect(request, directory)['status'], 'completed')
            (out/'gradient-decisions.csv').unlink()
            with self.assertRaisesRegex(ValueError, 'missing resource'): adapter.collect(request, directory)
            self.write_outputs(out, request['config']['resource_gradient_collection'])
            (out/'gradient-episodes.csv').write_text('complete,age,q32,q128\n')
            with self.assertRaisesRegex(ValueError, 'schema mismatch'): adapter.collect(request, directory)

    def test_incomplete_engine_request_keeps_original_failure_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)/'output'; out.mkdir()
            (out/'result.json').write_text(json.dumps({'schema_version': 1, 'status': 'invalid_request'}))
            self.assertEqual(EngineJob('game').collect(self.request(), directory)['status'], 'invalid_request')


if __name__ == '__main__': unittest.main()
