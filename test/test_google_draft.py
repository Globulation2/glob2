#!/usr/bin/env python3
"""Google drafts must preserve executable text and bound paid retry attempts."""
import importlib.util
import json
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

SPEC = importlib.util.spec_from_file_location('google_draft',
    Path(__file__).resolve().parents[1] / 'tools/localization/google_draft.py')
DRAFT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRAFT)


class FakeClient:
    project = 'test-project'
    location = 'global'

    def __init__(self):
        self.calls = []

    def call(self, method, payload):
        self.calls.append((method, payload))
        return {'translations': [{'translatedText': value.replace('Hello', 'Hei')}
                                 for value in payload['contents']]}


class GoogleDraftTest(unittest.TestCase):
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
        result = DRAFT.restore(source, DRAFT.protect(source).replace('Hello', 'Hei'))
        self.assertEqual(result, source.replace('Hello', 'Hei'))
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore(source, DRAFT.protect(source).replace('%0', '%1'))
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore('Hello {name}', 'Hei')
        self.assertEqual(DRAFT.restore(' / ', '/'), ' / ')
        self.assertEqual(DRAFT.restore('', ''), '')

    def test_bare_protocol_allows_norwegian_compound_suffix_without_becoming_url(self):
        source = 'Enter an https:// address.'
        self.assertEqual(DRAFT.restore(source, 'Skriv inn en https://-adresse.'),
                         'Skriv inn en https://-adresse.')
        # Bare scheme payload is unchanged, preserving previously paid cache keys.
        self.assertEqual(DRAFT.protect(source), source)
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore(source, 'Skriv inn en http://-adresse.')
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.restore('Open https://globulation2.org/help',
                          'Åpne https://globulation2.org/hjelp')
        self.assertEqual(DRAFT.restore('Open https://[::1]/help',
                                      'Åpne https://[::1]/help'), 'Åpne https://[::1]/help')

    def test_relative_routes_and_bare_web_addresses_cannot_be_translated(self):
        for source, altered in [('/ais/favourites', '/ais/favoritter'),
                                ('www.globulation2.org', 'www.globulation2.no'),
                                ('/play/?mode=serial', '/spill/?mode=serial')]:
            with self.subTest(source=source):
                protected = DRAFT.protect(source)
                self.assertIn('translate="no"', protected)
                self.assertEqual(DRAFT.restore(source, protected), source)
                with self.assertRaisesRegex(ValueError, 'protected tokens'):
                    DRAFT.restore(source, altered)
        self.assertEqual(DRAFT.protect('front/back application/octet-stream'),
                         'front/back application/octet-stream')

    def test_resume_reuses_exact_request_without_touching_editorial_work(self):
        editorial = self.output / 'drafts/nb/texts.nb.txt'
        editorial.parent.mkdir(parents=True)
        editorial.write_text('Independently edited')
        run = DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000)
        second_run = DRAFT.translate(self.plan, self.output, self.client, 'nb', 1)
        self.assertEqual(run, second_run)
        self.assertEqual(len(self.client.calls), 1)
        self.assertEqual(editorial.read_text(), 'Independently edited')
        self.assertEqual(json.loads((run / 'native.json').read_text()), {'[hello %0]': 'Hei %0'})
        # Target differences must never reuse a response from another translation request.
        DRAFT.translate(self.plan, self.output, self.client, 'no', 10000)
        self.assertEqual(len(self.client.calls), 2)

    def test_budget_is_checked_before_network_and_counts_uncertain_attempts(self):
        with self.assertRaisesRegex(ValueError, 'budget exhausted'):
            DRAFT.translate(self.plan, self.output, self.client, 'nb', 1)
        self.assertEqual(self.client.calls, [])
        size = sum(len(DRAFT.protect(text)) for text in self.plan['chunks'][0])
        self.client.call = lambda *_: (_ for _ in ()).throw(HTTPError('url', 503, 'busy', {}, None))
        with patch.object(DRAFT.time, 'sleep'):
            with self.assertRaisesRegex(ValueError, 'budget exhausted'):
                DRAFT.translate(self.plan, self.output, self.client, 'nb', size)
        attempts = list((self.output / 'google/nb/attempts').glob('*.json'))
        self.assertEqual(len(attempts), 1)
        self.assertEqual(json.loads(attempts[0].read_text())['characters'], size)

    def test_damaged_output_is_retained_but_never_emitted_as_a_catalog(self):
        self.client.call = lambda *_: {'translations': [{'translatedText': 'Hei'}, {'translatedText': 'Hei'}]}
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000)
        self.assertEqual(len(list((self.output / 'google/nb/responses').glob('*.json'))), 1)
        self.assertFalse((self.output / 'google/nb/runs').exists())

    def test_invalid_cache_requires_explicit_retry_and_preserves_rejected_output(self):
        self.client.call = lambda *_: {'translations': [{'translatedText': 'Hei'}, {'translatedText': 'Hei'}]}
        with self.assertRaises(ValueError):
            DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000)
        replacement = FakeClient()
        with self.assertRaisesRegex(ValueError, 'retry-invalid-responses'):
            DRAFT.translate(self.plan, self.output, replacement, 'nb', 10000)
        self.assertEqual(replacement.calls, [])
        DRAFT.translate(self.plan, self.output, replacement, 'nb', 10000, retry_invalid=True)
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


if __name__ == '__main__':
    unittest.main()
