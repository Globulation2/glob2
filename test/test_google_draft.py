#!/usr/bin/env python3
"""Cloud drafts must preserve work and enforce cost before any paid dispatch."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location('google_draft',
    Path(__file__).resolve().parents[1] / 'tools/localization/google_draft.py')
DRAFT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRAFT)
PROJECT = 'glob2-translation-test'


class GoogleDraftTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)

    def entry(self, text='Hello {name}', key='Hello {name}'):
        masked, protected = DRAFT.protect(text)
        return {'surface': 'online', 'key': key, 'category': None,
                'source': text, 'content': masked, 'protected': protected}

    def dispatch(self, project, payload):
        self.assertEqual(project, PROJECT)
        self.assertEqual(payload['sourceLanguageCode'], 'en')
        self.assertEqual(payload['targetLanguageCode'], 'ta')
        self.assertEqual(payload['mimeType'], 'text/plain')
        return {'translations': [{'translatedText': value.replace('Hello', 'வணக்கம்')}
                                 for value in payload['contents']]}

    def test_protects_repeated_tokens_and_contract_literals(self):
        source = r'%0 %0 {name} https://example.org/path `code` simVersion\n\n Ctrl+Enter Globulation 2'
        masked, protected = DRAFT.protect(source, ['simVersion'])
        self.assertNotIn('{name}', masked)
        self.assertNotIn('simVersion', masked)
        self.assertEqual(DRAFT.restore(masked, protected), source)
        with self.assertRaises(ValueError):
            DRAFT.restore(masked.replace('__G2P0__', ''), protected)
        with self.assertRaises(ValueError):
            DRAFT.restore(masked + '__G2P0__', protected)
        with self.assertRaises(ValueError):
            DRAFT.protect('reserved __G2P0__')

    def test_access_token_is_memory_only_and_refreshed_after_45_minutes(self):
        with patch.object(DRAFT, '_ACCESS_TOKEN', None), patch.object(DRAFT, '_TOKEN_OBTAINED_AT', 0):
            with patch.object(DRAFT.time, 'monotonic', side_effect=[100, 101, 2801]):
                with patch.object(DRAFT.subprocess, 'check_output', side_effect=['first-token\n', 'new-token\n']) as auth:
                    self.assertEqual(DRAFT.access_token(), 'first-token')
                    self.assertEqual(DRAFT.access_token(), 'first-token')
                    self.assertEqual(DRAFT.access_token(), 'new-token')
                    self.assertEqual(auth.call_count, 2)
        with patch.object(DRAFT, '_ACCESS_TOKEN', None):
            with patch.object(DRAFT.subprocess, 'check_output', side_effect=[OSError('auth unavailable'), 'recovered']) as auth:
                with self.assertRaises(OSError):
                    DRAFT.access_token()
                self.assertEqual(DRAFT.access_token(), 'recovered')
                self.assertEqual(auth.call_count, 2)

    def test_fragment_boundary_spaces_survive_provider_trimming(self):
        entry = self.entry(' · Hello {name} ')
        self.assertEqual(DRAFT.restore_entry('· வணக்கம் __G2P0__', entry), ' · வணக்கம் {name} ')

    def test_preflight_never_dispatches_and_counts_actual_masked_characters(self):
        entry = self.entry()
        dispatch = Mock(side_effect=AssertionError('Preflight must not make paid calls'))
        summary, results = DRAFT.translate(self.folder, [entry], PROJECT, 'ta', dispatch=dispatch)
        dispatch.assert_not_called()
        self.assertEqual(summary['new_request_characters'], len(entry['content']))
        self.assertEqual(results, {})
        self.assertFalse((self.folder / 'ledger.json').exists())

    def test_success_cache_resumes_without_dispatch_and_maps_duplicate_sources(self):
        first = self.entry()
        second = {**first, 'key': 'Same source in another entry'}
        dispatch = Mock(side_effect=self.dispatch)
        summary, result = DRAFT.translate(self.folder, [first, second], PROJECT, 'ta', dispatch=dispatch, execute=True)
        self.assertEqual(dispatch.call_count, 1)
        self.assertEqual(len(dispatch.call_args.args[1]['contents']), 1)
        identity = DRAFT.cache_key(first, PROJECT, 'ta')
        self.assertEqual(result[identity], 'வணக்கம் {name}')
        self.assertEqual(summary['characters_reserved'], len(first['content']))
        _, resumed = DRAFT.translate(self.folder, [first], PROJECT, 'ta', dispatch=dispatch, execute=True)
        self.assertEqual(dispatch.call_count, 1)
        self.assertEqual(resumed, result)
        self.assertNotEqual(identity, DRAFT.cache_key(first, PROJECT, 'hi'))
        raw = '\n'.join(path.read_text() for path in self.folder.rglob('*.json'))
        self.assertNotIn('Bearer', raw)
        self.assertNotIn('Authorization', raw)

    def test_failed_and_uncertain_requests_consume_budget_before_dispatch(self):
        entry = self.entry('Hello', 'Hello')
        limit = len(entry['content'])
        def failure(project, payload):
            ledger = json.loads((self.folder / 'ledger.json').read_text())
            self.assertEqual(ledger['attempts'][0]['characters_reserved'], limit)
            raise TimeoutError('Provider may have processed this request')
        with self.assertRaises(TimeoutError):
            DRAFT.translate(self.folder, [entry], PROJECT, 'ta', limit, failure, True)
        dispatch = Mock(side_effect=self.dispatch)
        with self.assertRaisesRegex(ValueError, 'Insufficient remaining budget'):
            DRAFT.translate(self.folder, [entry], PROJECT, 'ta', limit, dispatch, True)
        dispatch.assert_not_called()
        ledger = json.loads((self.folder / 'ledger.json').read_text())
        self.assertEqual(ledger['attempts'][0]['state'], 'failed_or_uncertain')
        with self.assertRaisesRegex(ValueError, 'project/budget differs'):
            DRAFT.translate(self.folder, [entry], PROJECT, 'ta', limit + 1, dispatch, True)

    def test_damaged_response_is_retained_and_not_retried_silently(self):
        entry = self.entry()
        dispatch = Mock(return_value={'translations': [{'translatedText': 'வணக்கம்'}]})
        summary, results = DRAFT.translate(self.folder, [entry], PROJECT, 'ta', dispatch=dispatch, execute=True)
        self.assertEqual(results, {})
        self.assertEqual(len(summary['repair_required']), 1)
        self.assertTrue((self.folder / 'responses/00001.json').exists())
        summary, results = DRAFT.translate(self.folder, [entry], PROJECT, 'ta', dispatch=dispatch, execute=True)
        self.assertEqual(len(summary['repair_required']), 1)
        self.assertEqual(dispatch.call_count, 1)

    def test_budget_and_response_count_fail_closed(self):
        entry = self.entry()
        dispatch = Mock(return_value={'translations': []})
        with self.assertRaises(ValueError):
            DRAFT.translate(self.folder, [entry], PROJECT, 'ta', limit=400001, dispatch=dispatch, execute=True)
        dispatch.assert_not_called()
        with self.assertRaisesRegex(ValueError, 'count differs'):
            DRAFT.translate(self.folder, [entry], PROJECT, 'ta', dispatch=dispatch, execute=True)
        self.assertEqual(dispatch.call_count, 1)

    def test_nonblank_work_is_not_selected_or_overwritten(self):
        source = {'native': {'[hello]': 'Hello'}, 'online': {'Hello {name}': 'Hello {name}'}}
        target = {'native': {'[hello]': 'ஏற்கெனவே திருத்தப்பட்டது'}, 'online': {'Hello {name}': ''}}
        entries = DRAFT.pending_entries(source, target, {})
        self.assertEqual(len(entries), 1)
        paths = {'native': self.folder / 'native.txt', 'online': self.folder / 'online.json'}
        paths['native'].write_text('[hello]\nஏற்கெனவே திருத்தப்பட்டது\n')
        paths['online'].write_text(json.dumps({'Hello {name}': 'இப்போது திருத்தப்பட்டது {name}'}))
        identity = DRAFT.cache_key(entries[0], PROJECT, 'ta')
        DRAFT.apply_results(paths, entries, {identity: 'வணக்கம் {name}'}, PROJECT, 'ta')
        self.assertEqual(json.loads(paths['online'].read_text())['Hello {name}'], 'இப்போது திருத்தப்பட்டது {name}')
        self.assertIn('ஏற்கெனவே திருத்தப்பட்டது', paths['native'].read_text())

    def test_plural_variants_and_pilot_filter(self):
        source = {'native': {'[hello]': 'Hello'}, 'online': {'{count} items': {'one': '{count} item', 'other': '{count} items'}}}
        targets = {'native': {'[hello]': ''}, 'online': {'{count} items': {'one': '', 'other': ''}}}
        entries = DRAFT.pending_entries(source, targets, {}, ['online:{count} items'])
        self.assertEqual({entry['category'] for entry in entries}, {'one', 'other'})
        self.assertEqual(len(entries), 2)
        for entry in entries:
            self.assertEqual(entry['protected'], ['{count}'])


if __name__ == '__main__':
    unittest.main()
