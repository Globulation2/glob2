#!/usr/bin/env python3
"""Cloud drafts retain tokens, bound requests and never overwrite reviewed text."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

SPEC = importlib.util.spec_from_file_location('cloud_draft', Path(__file__).resolve().parents[1] / 'tools/localization/cloud_draft.py')
CLOUD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CLOUD)


class ProtectionTest(unittest.TestCase):
    def test_tokens_newlines_html_and_invariants_round_trip(self):
        text = 'UTF-8 %0 {count}\\n\n<a href="/path"> & done'
        protected, tokens = CLOUD.protect(text, ['UTF-8'])
        self.assertNotIn('UTF-8', protected)
        self.assertNotIn('{count}', protected)
        self.assertEqual(CLOUD.restore(protected, tokens), text)
        for changed in (protected.replace('GLOB2KEEP0END', 'lost'), protected + 'GLOB2KEEP0END'):
            with self.assertRaises(ValueError):
                CLOUD.restore(changed, tokens)

    def test_split_keeps_literals_and_exact_original(self):
        text = ('word %0 two words ' * 400) + 'end'
        parts = CLOUD.split_text(text, ['two words'], limit=200)
        self.assertEqual(''.join(parts), text)
        self.assertTrue(all(len(CLOUD.protect(part, ['two words'])[0]) <= 200 for part in parts))
        self.assertTrue(all(not part.endswith('two ') for part in parts))
        with self.assertRaises(ValueError):
            CLOUD.split_text('a' * 5000)

    def test_response_entity_decoding_is_single_pass(self):
        protected, tokens = CLOUD.protect('A &lt; B & C')
        self.assertEqual(CLOUD.restore(protected, tokens), 'A &lt; B & C')


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.output = self.root / 'kit'
        native = '[hello %0]\nHello %0\n[reviewed]\nReviewed\n'
        online = {'Hello {name}': 'Hello {name}', 'UTF-8': 'UTF-8',
                  '{count} items': {'one': '{count} item', 'other': '{count} items'}}
        for path, data in [('data/texts.keys.txt', '[hello %0]\n[reviewed]\n'), ('data/texts.en.txt', native),
                           ('platform/packages/i18n/locales/en.json', json.dumps(online)),
                           ('platform/packages/i18n/invariants.json', json.dumps({'UTF-8': ['UTF-8']})),
                           ('platform/packages/i18n/plural-sources.json', '[]')]:
            target = self.root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(data)
        self.root_patch = patch.object(CLOUD.PROGRAM, 'ROOT', self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        # Use an already supported locale; runner itself has no language-specific assumptions.
        with patch.object(CLOUD.PROGRAM.subprocess, 'check_output', return_value='revision'):
            CLOUD.PROGRAM.prepare(self.output, 10, self.root, locales=['sw'])
        with patch.object(CLOUD.PROGRAM, 'plural_categories', return_value=['other']):
            CLOUD.PROGRAM.draft(self.output, 'sw')
        self.paths = CLOUD.PROGRAM.catalog_paths(self.output, 'sw')
        self.paths['native'].write_text('[hello %0]\n\n[reviewed]\nSudah disemak\n')

    @staticmethod
    def translated(_project, body):
        return {'translations': [{'translatedText': value.replace('Hello', 'Helo').replace('items', 'barang')}
                                 for value in body['contents']]}

    def test_dry_run_does_not_call_provider_and_resume_preserves_edits(self):
        with patch.object(CLOUD, 'translate', side_effect=self.translated) as api:
            report = CLOUD.run(self.output, 'sw', 'test-project', dry_run=True)
            self.assertEqual(report['pending_values'], 4)
            api.assert_not_called()
            report = CLOUD.run(self.output, 'sw', 'test-project')
            self.assertEqual(report['requests'], 1)
            self.assertLessEqual(sum(map(len, api.call_args.args[1]['contents'])), CLOUD.LIMIT)
            self.assertIn('Sudah disemak', self.paths['native'].read_text())
            self.assertIn('Helo %0', self.paths['native'].read_text())
            self.assertEqual(json.loads(self.paths['online'].read_text())['UTF-8'], 'UTF-8')
            report = CLOUD.run(self.output, 'sw', 'test-project')
            self.assertEqual(report['requests'], 0)
            self.assertEqual(api.call_count, 1)

    def test_pilot_selection_limits_paid_requests(self):
        with patch.object(CLOUD, 'translate', side_effect=self.translated) as api:
            report = CLOUD.run(self.output, 'sw', 'test-project', selected_keys=['online:Hello {name}'])
            self.assertEqual(report['pending_values'], 1)
            self.assertEqual(len(api.call_args.args[1]['contents']), 1)
            self.assertNotIn('Helo %0', self.paths['native'].read_text())
            with self.assertRaises(ValueError):
                CLOUD.run(self.output, 'sw', 'test-project', selected_keys=['online:unknown'])

    def test_response_cache_resumes_after_catalog_write_failure(self):
        with patch.object(CLOUD, 'translate', side_effect=self.translated) as api:
            with patch.object(CLOUD, 'save_catalogs', side_effect=OSError('disk failure')):
                with self.assertRaises(OSError):
                    CLOUD.run(self.output, 'sw', 'test-project')
            report = CLOUD.run(self.output, 'sw', 'test-project')
            self.assertEqual(report['cache_hits'], 1)
            self.assertEqual(api.call_count, 1)

    def test_partial_long_entry_resumes_when_completed_neighbor_changes_batch(self):
        source = '[prefix]\nH\n[long]\n' + ('Hello ' * 1000) + '\n'
        (self.root / 'data/texts.keys.txt').write_text('[prefix]\n[long]\n')
        (self.root / 'data/texts.en.txt').write_text(source)
        output = self.root / 'large-kit'
        with patch.object(CLOUD.PROGRAM.subprocess, 'check_output', return_value='revision'):
            CLOUD.PROGRAM.prepare(output, 10, self.root, locales=['sw'])
        with patch.object(CLOUD.PROGRAM, 'plural_categories', return_value=['other']):
            CLOUD.PROGRAM.draft(output, 'sw')
        calls = []

        def provider(project, body):
            calls.append(body)
            if len(calls) == 2:
                raise RuntimeError('interrupted before second response')
            return self.translated(project, body)

        with patch.object(CLOUD, 'translate', side_effect=provider):
            with self.assertRaises(RuntimeError):
                CLOUD.run(output, 'sw', 'test-project')
            self.assertEqual(len(calls[0]['contents']), 2)
            report = CLOUD.run(output, 'sw', 'test-project')
        self.assertGreaterEqual(report['cache_hits'], 1)
        # First chunk survives in the original batch evidence despite removal of prefix.
        first_chunk = calls[0]['contents'][1]
        self.assertFalse(any(first_chunk in body['contents'] for body in calls[2:]))
        self.assertEqual(len(calls), 3)
        self.assertIn('Helo Helo', CLOUD.PROGRAM.catalog_paths(output, 'sw')['native'].read_text())

    def test_malformed_response_does_not_change_drafts(self):
        before = {name: path.read_bytes() for name, path in self.paths.items()}
        for response in ({'translations': []}, None,
                         {'translations': [None] * 4},
                         {'translations': [{'translatedText': ''}] * 4},
                         {'translations': [{'translatedText': '<b></b>'}] * 4}):
            with self.subTest(response=response):
                # Retained invalid provider evidence must fail safely on resume too.
                cache = self.output / 'cloud/sw'
                for path in cache.glob('*.json'):
                    path.unlink()
                with patch.object(CLOUD, 'translate', return_value=response):
                    with self.assertRaises(ValueError):
                        CLOUD.run(self.output, 'sw', 'test-project')
                self.assertEqual(before, {name: path.read_bytes() for name, path in self.paths.items()})
                with patch.object(CLOUD, 'translate') as api:
                    with self.assertRaises(ValueError):
                        CLOUD.run(self.output, 'sw', 'test-project')
                    api.assert_not_called()

    def test_changed_frozen_source_rejected_before_paid_call(self):
        path = self.output / 'source/native.json'
        path.write_text(json.dumps({'[hello %0]': 'Different', '[reviewed]': 'Reviewed'}))
        with patch.object(CLOUD, 'translate') as api:
            with self.assertRaises(ValueError):
                CLOUD.run(self.output, 'sw', 'test-project')
            api.assert_not_called()

    def test_retry_is_bounded_and_permanent_errors_are_not_retried(self):
        with patch.object(CLOUD.subprocess, 'check_output', return_value='secret-token'), \
                patch.object(CLOUD, 'urlopen', side_effect=HTTPError('url', 503, 'Unavailable', {}, None)) as request, \
                patch.object(CLOUD.time, 'sleep'):
            with self.assertRaises(RuntimeError):
                CLOUD.translate('test-project', {}, attempts=3)
            self.assertEqual(request.call_count, 3)
        with patch.object(CLOUD.subprocess, 'check_output', return_value='secret-token'), \
                patch.object(CLOUD, 'urlopen', side_effect=HTTPError('url', 403, 'Forbidden', {}, None)) as request:
            with self.assertRaises(RuntimeError):
                CLOUD.translate('test-project', {}, attempts=3)
            self.assertEqual(request.call_count, 1)


if __name__ == '__main__':
    unittest.main()
