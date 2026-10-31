#!/usr/bin/env python3
"""Paid drafting preserves contracts, review work and conservative accounting."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('google_draft',
    Path(__file__).resolve().parents[1] / 'tools/localization/google_draft.py')
DRAFT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRAFT)


class GoogleDraftTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / 'kit'
        for name in DRAFT.PROGRAM.SOURCE_PATHS:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('{}')
        (self.root / 'data/texts.keys.txt').write_text('[hello]\n[value %0]\n')
        (self.root / 'data/texts.en.txt').write_text('[hello]\nHello\n[value %0]\n Value %0 \n')
        (self.root / 'platform/packages/i18n/locales/en.json').write_text(json.dumps({
            'Hello': 'Hello', '{count} items': {'one': '{count} item', 'other': '{count} items'},
            'Invalid mapId.': 'Invalid mapId.'}))
        (self.root / 'platform/packages/i18n/invariants.json').write_text(json.dumps({'Invalid mapId.': ['mapId']}))
        with patch.object(DRAFT.PROGRAM.subprocess, 'check_output', return_value='test'):
            DRAFT.PROGRAM.prepare(self.output, root=self.root, locales=('fil',))
        with patch.object(DRAFT.PROGRAM, 'plural_categories', return_value=['one', 'other']):
            DRAFT.PROGRAM.draft(self.output, 'fil')
        self.calls = []

    def send(self, project, locale, texts):
        self.calls.append((project, locale, texts))
        return {'translations': [{'translatedText': value.replace('Hello', 'Kumusta').replace('Value', 'Halaga')
                                  .replace('Invalid', 'Di-wastong').replace('items', 'bagay').replace('item', 'bagay')}
                                 for value in texts]}

    def run_draft(self, **kwargs):
        with patch.object(DRAFT.PROGRAM, 'plural_categories', return_value=['one', 'other']):
            return DRAFT.run(self.output, root=self.root, send=kwargs.pop('send', self.send), **kwargs)

    def test_preserves_tokens_whitespace_and_technical_invariants(self):
        self.assertEqual(DRAFT.restore('   ', 'ignored'), '   ')
        route = '/ais/favourites'
        self.assertIn('translate="no"', DRAFT.protect(route))
        self.assertEqual(DRAFT.restore(route, DRAFT.protect(route)), route)
        original = '  Value %0 {count} mapId \\n next\nline  '
        protected = DRAFT.protect(original, ['mapId'])
        self.assertIn('translate="no"', protected)
        self.assertEqual(DRAFT.restore(original, protected.replace('Value', 'Halaga'), ['mapId']),
                         original.replace('Value', 'Halaga'))
        for changed in [protected.replace('%0', '%1'), protected.replace('mapId', 'mapa')]:
            with self.assertRaises(ValueError):
                DRAFT.restore(original, changed, ['mapId'])

    def test_deduplicates_and_resume_does_not_overwrite_editor(self):
        first = self.run_draft()
        # Native/online Hello share one request; plural values remain distinct.
        self.assertEqual(sum(len(call[2]) for call in self.calls), 5)
        paths = DRAFT.PROGRAM.catalog_paths(self.output, 'fil')
        paths['native'].write_text('[hello]\nMabuhay\n[value %0]\n\n')
        self.calls.clear()
        second = self.run_draft()
        self.assertEqual(self.calls, [])
        native, errors = DRAFT.PROGRAM.CHECKER.read_table(paths['native'])
        self.assertEqual(errors, [])
        self.assertEqual(native['[hello]'], 'Mabuhay')
        self.assertEqual(native['[value %0]'], ' Halaga %0 ')
        self.assertEqual(first['estimated_usd_before_credits'], second['estimated_usd_before_credits'])

    def test_failed_request_is_charged_conservatively_before_network(self):
        def fail(project, locale, texts):
            records = (self.output / 'google/fil/attempts.jsonl').read_text().splitlines()
            self.assertEqual(len(records), 1)
            reservation = json.loads(records[0])
            payload = json.loads((self.output / 'google/fil/requests' / (reservation['id'] + '.json')).read_text())
            self.assertEqual(payload['contents'], texts)
            self.assertEqual(sum(map(len, texts)), reservation['characters'])
            self.assertNotIn('Authorization', payload)
            self.assertEqual(payload['targetLanguageCode'], 'fil')
            self.assertEqual(len(reservation['runner_sha256']), 64)
            raise OSError('network failed')
        with self.assertRaises(OSError):
            self.run_draft(send=fail)
        first = json.loads((self.output / 'google/fil/attempts.jsonl').read_text())
        summary = self.run_draft()
        self.assertEqual(summary['attempted_characters_including_failures'], first['characters'] * 2)

    def test_budget_prevents_network_and_rejects_unbounded_budget(self):
        with self.assertRaises(ValueError):
            self.run_draft(max_usd=0.000001)
        self.assertEqual(self.calls, [])
        for budget in [13, float('nan'), float('inf')]:
            with self.assertRaises(ValueError):
                self.run_draft(max_usd=budget)
        with self.assertRaises(ValueError):
            self.run_draft(limit=0)

    def test_source_or_snapshot_change_prevents_paid_requests(self):
        snapshot = self.output / 'source/native.json'
        snapshot.write_text('{}')
        with self.assertRaises(ValueError):
            self.run_draft()
        self.assertEqual(self.calls, [])

    def test_bad_output_is_retained_but_not_applied(self):
        def damaged(project, locale, texts):
            return {'translations': [{'translatedText': text.replace('%0', '%2')} for text in texts]}
        summary = self.run_draft(send=damaged)
        self.assertEqual(summary['rejected_entries'], 1)
        paths = DRAFT.PROGRAM.catalog_paths(self.output, 'fil')
        native, _ = DRAFT.PROGRAM.CHECKER.read_table(paths['native'])
        self.assertEqual(native['[value %0]'], '')
        self.assertTrue(list((self.output / 'google/fil/responses').glob('*.json')))

    def test_pilot_limits_distinct_requests(self):
        summary = self.run_draft(limit=2)
        self.assertEqual(summary['selected_unique_requests'], 2)
        self.assertEqual(sum(len(call[2]) for call in self.calls), 2)

    def test_malformed_response_counts_attempt_without_losing_resume(self):
        with self.assertRaises(ValueError):
            self.run_draft(send=lambda *args: {'translations': []})
        state = self.output / 'google/fil'
        self.assertEqual(len(list((state / 'responses').glob('*.json'))), 1)
        self.assertTrue((state / 'attempts.jsonl').exists())
        self.assertFalse((state / 'runner.lock').exists())
        self.run_draft()
        self.assertEqual(len((state / 'attempts.jsonl').read_text().splitlines()), 2)

    def test_punctuation_only_values_do_not_use_paid_api(self):
        source = self.root / 'platform/packages/i18n/locales/en.json'
        source.write_text(json.dumps({' / {slot0}': ' / {slot0}'}))
        # New source requires a freshly prepared kit, never source hash tampering.
        self.output = self.root / 'punctuation-kit'
        with patch.object(DRAFT.PROGRAM.subprocess, 'check_output', return_value='test'):
            DRAFT.PROGRAM.prepare(self.output, root=self.root, locales=('fil',))
        with patch.object(DRAFT.PROGRAM, 'plural_categories', return_value=['one', 'other']):
            DRAFT.PROGRAM.draft(self.output, 'fil')
        self.run_draft()
        self.assertEqual(sum(len(call[2]) for call in self.calls), 2)
        target = json.loads(DRAFT.PROGRAM.catalog_paths(self.output, 'fil')['online'].read_text())
        self.assertEqual(target[' / {slot0}'], ' / {slot0}')

    def test_concurrent_writer_is_rejected(self):
        lock = self.output / 'google/fil/runner.lock'
        with DRAFT.exclusive(lock):
            with self.assertRaises(FileExistsError):
                self.run_draft()


if __name__ == '__main__':
    unittest.main()
