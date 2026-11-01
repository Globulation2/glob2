#!/usr/bin/env python3
"""Cloud drafts must preserve work and enforce cost before any paid dispatch."""
import importlib.util
import json
import io
from urllib.error import HTTPError
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location('google_draft',
    Path(__file__).resolve().parents[1] / 'tools/localization/google_draft.py')
DRAFT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRAFT)
PROJECT = 'glob2-translation-test'


class PlainGoogleDraftTest(unittest.TestCase):
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


    def test_plain_dispatch_refuses_existing_html_paid_ledger(self):
        prior = self.folder / 'google' / 'nb' / 'attempts'
        prior.mkdir(parents=True)
        (prior / 'paid.json').write_text('{"characters":100}')
        dispatch = Mock()
        with self.assertRaisesRegex(ValueError, 'other protection mode'):
            DRAFT.translate(self.folder / 'google-nmt', [self.entry()], PROJECT, 'ta',
                            dispatch=dispatch, execute=True)
        dispatch.assert_not_called()


class FakeClient:
    project = 'test-project'
    location = 'global'

    def __init__(self):
        self.calls = []

    def call(self, method, payload):
        self.calls.append((method, payload))
        return {'translations': [{'translatedText': value.replace('Hello', 'Hei')}
                                 for value in payload['contents']]}


class HtmlGoogleDraftTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.client = FakeClient()
        self.plan = {'locale': 'nb', 'source_sha256': 'source-hash',
                     'chunks': [['Hello {name}', 'Hello %0']], 'entries': [
                         {'surface': 'native', 'key': '[hello %0]', 'variant': '', 'source': 'Hello %0'},
                         {'surface': 'online', 'key': 'Hello {name}', 'variant': '', 'source': 'Hello {name}'}]}

    def test_protection_preserves_placeholders_markup_urls_and_fragments(self):
        source = r'  Hello %0 / {slot0}: <b>https://globulation2.org/help</b>\n  '
        result = DRAFT.restore_html(source, DRAFT.protect_html(source).replace('Hello', 'Hei'))
        self.assertEqual(result, source.replace('Hello', 'Hei'))
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore_html(source, DRAFT.protect_html(source).replace('%0', '%1'))
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore_html('Hello {name}', 'Hei')
        self.assertEqual(DRAFT.restore_html(' / ', '/'), ' / ')
        self.assertEqual(DRAFT.restore_html('', ''), '')

    def test_bare_protocol_allows_norwegian_compound_suffix_without_becoming_url(self):
        source = 'Enter an https:// address.'
        self.assertEqual(DRAFT.restore_html(source, 'Skriv inn en https://-adresse.'),
                         'Skriv inn en https://-adresse.')
        # Bare scheme payload is unchanged, preserving previously paid cache keys.
        self.assertEqual(DRAFT.protect_html(source), source)
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore_html(source, 'Skriv inn en http://-adresse.')
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore_html('Open https://globulation2.org/help',
                          'Åpne https://globulation2.org/hjelp')
        self.assertEqual(DRAFT.restore_html('Open https://[::1]/help',
                                      'Åpne https://[::1]/help'), 'Åpne https://[::1]/help')

    def test_relative_routes_and_bare_web_addresses_cannot_be_translated(self):
        for source, altered in [('/ais/favourites', '/ais/favoritter'),
                                ('www.globulation2.org', 'www.globulation2.no'),
                                ('/play/?mode=serial', '/spill/?mode=serial')]:
            with self.subTest(source=source):
                protected = DRAFT.protect_html(source)
                self.assertIn('translate="no"', protected)
                self.assertEqual(DRAFT.restore_html(source, protected), source)
                with self.assertRaisesRegex(ValueError, 'protected tokens'):
                    DRAFT.restore_html(source, altered)
        self.assertEqual(DRAFT.protect_html('front/back application/octet-stream'),
                         'front/back application/octet-stream')

    def test_resume_reuses_exact_request_without_touching_editorial_work(self):
        editorial = self.output / 'drafts/nb/texts.nb.txt'
        editorial.parent.mkdir(parents=True)
        editorial.write_text('Independently edited')
        run = DRAFT.translate_html(self.plan, self.output, self.client, 'nb', 10000)
        second_run = DRAFT.translate_html(self.plan, self.output, self.client, 'nb', 1)
        self.assertEqual(run, second_run)
        self.assertEqual(len(self.client.calls), 1)
        self.assertEqual(editorial.read_text(), 'Independently edited')
        self.assertEqual(json.loads((run / 'native.json').read_text()), {'[hello %0]': 'Hei %0'})
        # Target differences must never reuse a response from another translation request.
        DRAFT.translate_html(self.plan, self.output, self.client, 'no', 10000)
        self.assertEqual(len(self.client.calls), 2)

    def test_budget_is_checked_before_network_and_counts_uncertain_attempts(self):
        with self.assertRaisesRegex(ValueError, 'budget exhausted'):
            DRAFT.translate_html(self.plan, self.output, self.client, 'nb', 1)
        self.assertEqual(self.client.calls, [])
        size = sum(len(DRAFT.protect_html(text)) for text in self.plan['chunks'][0])
        self.client.call = lambda *_: (_ for _ in ()).throw(HTTPError('url', 503, 'busy', {}, None))
        with patch.object(DRAFT.time, 'sleep'):
            with self.assertRaisesRegex(ValueError, 'budget exhausted'):
                DRAFT.translate_html(self.plan, self.output, self.client, 'nb', size)
        attempts = list((self.output / 'google/nb/attempts').glob('*.json'))
        self.assertEqual(len(attempts), 1)
        self.assertEqual(json.loads(attempts[0].read_text())['characters'], size)

    def test_damaged_output_is_retained_but_never_emitted_as_a_catalog(self):
        self.client.call = lambda *_: {'translations': [{'translatedText': 'Hei'}, {'translatedText': 'Hei'}]}
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.translate_html(self.plan, self.output, self.client, 'nb', 10000)
        self.assertEqual(len(list((self.output / 'google/nb/responses').glob('*.json'))), 1)
        self.assertFalse((self.output / 'google/nb/runs').exists())

    def test_invalid_cache_requires_explicit_retry_and_preserves_rejected_output(self):
        self.client.call = lambda *_: {'translations': [{'translatedText': 'Hei'}, {'translatedText': 'Hei'}]}
        with self.assertRaises(ValueError):
            DRAFT.translate_html(self.plan, self.output, self.client, 'nb', 10000)
        replacement = FakeClient()
        with self.assertRaisesRegex(ValueError, 'retry-invalid-responses'):
            DRAFT.translate_html(self.plan, self.output, replacement, 'nb', 10000)
        self.assertEqual(replacement.calls, [])
        DRAFT.translate_html(self.plan, self.output, replacement, 'nb', 10000, retry_invalid=True)
        self.assertEqual(len(replacement.calls), 1)
        self.assertEqual(len(list((self.output / 'google/nb/rejected').glob('*.json'))), 1)
        self.assertEqual(len(list((self.output / 'google/nb/attempts').glob('*.json'))), 2)

    def test_auth_token_is_cached_and_refreshed_once_on_unauthorized(self):
        client = DRAFT.GoogleClient('test-project')
        responses = [io.StringIO('{}'), HTTPError('url', 401, 'expired', {}, io.BytesIO()),
                     io.StringIO('{}'), io.StringIO('{}')]
        with patch.object(DRAFT.subprocess, 'check_output', side_effect=['token-a', 'token-b']) as auth:
            with patch.object(DRAFT, 'urlopen', side_effect=responses) as network:
                client.call('translateText', {})
                client.call('translateText', {})
                client.call('translateText', {})
        self.assertEqual(auth.call_count, 2)
        self.assertEqual(network.call_count, 4)

    def test_provider_bokmal_code_prefers_nb_and_falls_back_to_no(self):
        client = DRAFT.GoogleClient('test-project')
        with patch.object(client, 'request', return_value={'languages': [
                {'languageCode': 'no', 'supportTarget': True}]}):
            self.assertEqual(client.target('nb'), 'no')
        with patch.object(client, 'request', return_value={'languages': [
                {'languageCode': 'no', 'supportTarget': True},
                {'languageCode': 'nb', 'supportTarget': True}]}):
            self.assertEqual(client.target('nb'), 'nb')

    def test_plural_forms_and_deduplication_keep_key_mapping(self):
        DRAFT.PROGRAM.write_json(self.output / 'manifest.json', {
            'locales': ['nb'], 'source_sha256': 'frozen',
            'batches': [{'id': 'online-001', 'path': 'batches/online-001.json'}]})
        DRAFT.PROGRAM.write_json(self.output / 'batches/online-001.json', {
            'source_sha256': 'frozen', 'surface': 'online', 'entries': [
                {'key': '{count} item', 'source': {'one': '{count} item', 'other': '{count} items'}},
                {'key': 'same source', 'source': '{count} item'}]})
        plan = DRAFT.make_plan(self.output, 'nb')
        self.assertEqual(len(plan['entries']), 3)
        self.assertEqual(sum(map(len, plan['chunks'])), 2)
        with self.assertRaisesRegex(ValueError, 'Unknown batch'):
            DRAFT.make_plan(self.output, 'nb', ['missing'])


    def test_html_dispatch_refuses_existing_plain_reserved_budget(self):
        prior = self.output / 'google-nmt'
        prior.mkdir()
        (prior / 'ledger.json').write_text('{"attempts":[{"characters_reserved":100}]}')
        with self.assertRaisesRegex(ValueError, 'other protection mode'):
            DRAFT.translate_html(self.plan, self.output, self.client, 'nb', 10000)
        self.assertEqual(self.client.calls, [])


class BackendIsolationTest(unittest.TestCase):
    def test_plain_and_html_reject_existing_filipino_editorial_campaign(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            ledger = output / 'google/fil/attempts.jsonl'
            ledger.parent.mkdir(parents=True)
            ledger.write_text('{"characters": 10}\n', encoding='utf-8')
            for mode in ('plain', 'html'):
                with self.subTest(mode=mode):
                    with self.assertRaisesRegex(ValueError, 'paid editorial ledger'):
                        DRAFT.assert_backend_budget(output, mode)

    def test_plain_cannot_reset_segmented_paid_ledger(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / 'google-budget.json').write_text('{"attempts":[{"state":"dispatched_or_uncertain"}]}')
            with self.assertRaisesRegex(ValueError, 'other protection mode'):
                DRAFT.assert_backend_budget(output, 'plain')

    def test_segmented_cannot_reset_plain_paid_ledger(self):
        import test_google_draft_segmented as segmented
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / 'google-nmt').mkdir()
            (output / 'google-nmt' / 'ledger.json').write_text('{"attempts":[{"characters_reserved":10}]}')
            with self.assertRaisesRegex(ValueError, 'other protection mode'):
                with segmented.DRAFT.kit_lock(output):
                    self.fail('Cannot acquire paid writer for another backend')


from test_google_draft_segmented import (SegmentedGoogleDraftTest, GooglePlanDraftTest,
    GoogleMergedApiTest, NorwegianGoogleDraftTest, FilipinoEditorialGoogleDraftTest, GoogleWorkflowDispatchTest)


if __name__ == '__main__':
    unittest.main()
