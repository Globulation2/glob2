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


class NorwegianGoogleDraftTest(unittest.TestCase):
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
        self.assertEqual(DRAFT.restore_editorial('   ', 'ignored'), '   ')
        route = '/ais/favourites'
        self.assertIn('translate="no"', DRAFT.protect_editorial(route))
        self.assertEqual(DRAFT.restore_editorial(route, DRAFT.protect_editorial(route)), route)
        original = '  Value %0 {count} mapId \\n next\nline  '
        protected = DRAFT.protect_editorial(original, ['mapId'])
        self.assertIn('translate="no"', protected)
        self.assertEqual(DRAFT.restore_editorial(original, protected.replace('Value', 'Halaga'), ['mapId']),
                         original.replace('Value', 'Halaga'))
        for changed in [protected.replace('%0', '%1'), protected.replace('mapId', 'mapa')]:
            with self.assertRaises(ValueError):
                DRAFT.restore_editorial(original, changed, ['mapId'])

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
