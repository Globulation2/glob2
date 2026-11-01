#!/usr/bin/env python3
"""Paid calls must remain bounded, resumable and safe for format contracts."""
import importlib.util
import json
import io
from urllib.error import HTTPError
import os
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('google_draft',
    Path(__file__).resolve().parents[1] / 'tools/localization/google_draft_segmented.py')
DRAFT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRAFT)


class SegmentedGoogleDraftTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_preserves_placeholders_invariants_urls_and_fragment_spaces(self):
        source = ' with {slot0}: %12\nJavaScript matchId https://example.com/a '
        masked, protected = DRAFT.mask(source, ['matchId'])
        self.assertNotIn('{slot0}', masked)
        self.assertNotIn('matchId', masked)
        self.assertEqual(DRAFT.unmask(masked.strip(), protected, source), source)
        with self.assertRaises(ValueError):
            DRAFT.unmask(masked.replace('ZXQPH0QXZ', ''), protected, source)
        with self.assertRaises(ValueError):
            DRAFT.unmask(masked + ' ZXQPH0QXZ', protected, source)
        with self.assertRaises(ValueError):
            DRAFT.mask('ZXQPH0QXZ')

    def test_budget_prevents_call_past_cap_and_is_preserved_after_restart(self):
        path = self.root / 'ledger.json'
        budget = DRAFT.Budget(path, 'project', '0.0001')
        calls = []

        def translate(project, payload):
            self.assertTrue(path.exists(), 'reservation must be durable before API dispatch')
            self.assertEqual(json.loads(path.read_text())['attempts'][0]['state'], 'dispatched_or_uncertain')
            calls.append(payload)
            return {'translations': [{'translatedText': 'परीक्षण'}]}

        DRAFT.fetch('project', {'contents': ['hello']}, self.root / 'cache', budget, translate)
        resumed = DRAFT.Budget(path, 'project', '0.0001')
        with self.assertRaises(ValueError):
            DRAFT.fetch('project', {'contents': ['again']}, self.root / 'cache', resumed, translate)
        self.assertEqual(len(calls), 1)
        with self.assertRaises(ValueError):
            DRAFT.Budget(path, 'different-project', '9')
        with self.assertRaises(ValueError):
            DRAFT.Budget(path, 'project', '10')

    def test_cached_response_does_not_spend_or_dispatch_again(self):
        budget = DRAFT.Budget(self.root / 'ledger.json', 'project', '9')
        payload = {'contents': ['hello']}
        calls = []

        def translate(project, body):
            calls.append(body)
            return {'translations': [{'translatedText': 'नमस्कार'}]}

        first = DRAFT.fetch('project', payload, self.root / 'cache', budget, translate)
        second = DRAFT.fetch('project', payload, self.root / 'cache', budget, translate)
        self.assertEqual(first, second)
        self.assertEqual(len(calls), 1)
        self.assertEqual(budget.characters, 5)
        cache = next((self.root / 'cache').glob('*.json'))
        value = json.loads(cache.read_text())
        value['response']['translations'][0]['translatedText'] = 'corrupted'
        cache.write_text(json.dumps(value))
        with self.assertRaises(ValueError):
            DRAFT.fetch('project', payload, self.root / 'cache', budget, translate)
        self.assertEqual(len(calls), 1)

    def test_uncertain_failure_counts_again_on_explicit_rerun(self):
        budget = DRAFT.Budget(self.root / 'ledger.json', 'project', '9')

        def timeout(project, payload):
            raise TimeoutError('no response')

        for _ in range(2):
            with self.assertRaises(TimeoutError):
                DRAFT.fetch('project', {'contents': ['hello']}, self.root / 'cache', budget, timeout)
        self.assertEqual(budget.characters, 10)
        self.assertEqual(len(budget.value['attempts']), 2)
        self.assertTrue(all(attempt['state'] == 'dispatched_or_uncertain'
                            for attempt in budget.value['attempts']))

    def test_ledger_directory_sync_failure_prevents_paid_dispatch(self):
        budget = DRAFT.Budget(self.root / 'ledger.json', 'project', '9')
        calls = []
        with patch.object(DRAFT, 'fsync_directory', side_effect=OSError('directory sync failed')):
            with self.assertRaisesRegex(OSError, 'directory sync failed'):
                DRAFT.fetch('project', {'contents': ['hello']}, self.root / 'cache', budget,
                            lambda project, payload: calls.append(payload))
        self.assertEqual(calls, [])
        self.assertFalse((self.root / 'cache').exists())

    def test_external_edit_during_request_is_preserved_and_response_cached(self):
        paths = DRAFT.PROGRAM.catalog_paths(self.root, 'mr')
        paths['native'].parent.mkdir(parents=True)
        paths['native'].write_text('[hello]\n\n[edited]\nold editorial\n', encoding='utf-8')
        DRAFT.atomic_json(paths['online'], {'Edited': 'old online editorial'})
        DRAFT.atomic_json(self.root / 'manifest.json', {'source_sha256': 'test'})
        targets = {'native': {'[hello]': '', '[edited]': 'old editorial'},
                   'online': {'Edited': 'old online editorial'}}
        source = 'Hello'
        masked, protected = DRAFT.mask(source)
        items = [{'id': 'native:[hello]', 'surface': 'native', 'key': '[hello]', 'category': None,
                  'source': source, 'masked': masked, 'protected': protected,
                  'pieces': [{'source': source, 'masked': masked, 'protected': protected}]}]

        def translate(project, payload):
            paths['native'].write_text('[hello]\n\n[edited]\nnew editorial\n', encoding='utf-8')
            DRAFT.atomic_json(paths['online'], {'Edited': 'new online editorial'})
            return {'translations': [{'translatedText': 'नमस्कार'}]}

        with patch.object(DRAFT, 'entries', return_value=(paths, targets, items)):
            with self.assertRaisesRegex(ValueError, 'catalogs changed'):
                DRAFT.run(self.root, 'mr', 'project', execute=True, translate=translate)
        native, errors = DRAFT.PROGRAM.CHECKER.read_table(paths['native'])
        self.assertEqual(errors, [])
        self.assertEqual(native, {'[hello]': '', '[edited]': 'new editorial'})
        self.assertEqual(json.loads(paths['online'].read_text()), {'Edited': 'new online editorial'})
        cached = json.loads(next((self.root / 'google-responses').glob('*.json')).read_text())
        self.assertEqual(cached['response']['translations'][0]['translatedText'], 'नमस्कार')
        ledger = json.loads((self.root / 'google-budget.json').read_text())
        self.assertEqual(ledger['attempts'][0]['state'], 'response_received')

    def test_request_bounds_and_representative_pilot(self):
        items = [{'masked': 'a' * 10, 'id': str(i), 'domain': str(i % 3)} for i in range(15)]
        groups = list(DRAFT.batches(items, max_characters=25, max_items=3))
        self.assertTrue(all(sum(len(item['masked']) for item in group) <= 25 for group in groups))
        with self.assertRaises(ValueError):
            list(DRAFT.batches(items, max_characters=5))
        selected = DRAFT.pilot_entries(items, 5)
        self.assertEqual([item['domain'] for item in selected], ['0', '1', '2', '0', '1'])

    def test_bad_item_retains_valid_siblings_without_repair(self):
        paths = DRAFT.PROGRAM.catalog_paths(self.root, 'mr')
        paths['native'].parent.mkdir(parents=True)
        paths['native'].write_text('[good]\n\n[bad]\n\n[later]\n\n', encoding='utf-8')
        DRAFT.atomic_json(paths['online'], {})
        DRAFT.atomic_json(self.root / 'manifest.json', {'source_sha256': 'test'})
        targets = {'native': {'[good]': '', '[bad]': '', '[later]': ''}, 'online': {}}
        items = []
        for key, source in [('[good]', 'Hello'), ('[bad]', 'Value %0'), ('[later]', 'Later ' + 'a' * 3990)]:
            masked, protected = DRAFT.mask(source)
            items.append({'id': 'native:' + key, 'surface': 'native', 'key': key, 'category': None,
                          'source': source, 'masked': masked, 'protected': protected,
                          'pieces': [{'source': source, 'masked': masked, 'protected': protected}]})

        def translate(project, payload):
            return {'translations': [{'translatedText': 'नमस्कार' if text == 'Hello' else
                                     'नंतर' if text.startswith('Later ') else 'damaged'}
                                     for text in payload['contents']]}

        with patch.object(DRAFT, 'entries', return_value=(paths, targets, items)):
            summary = DRAFT.run(self.root, 'mr', 'project', execute=True, translate=translate)
        self.assertEqual(len(summary['rejected_entries']), 1)
        native, errors = DRAFT.PROGRAM.CHECKER.read_table(paths['native'])
        self.assertEqual(errors, [])
        self.assertEqual(native, {'[good]': 'नमस्कार', '[bad]': '', '[later]': 'नंतर'})
        failures = json.loads(next((self.root / 'google-failures').glob('*.json')).read_text())
        self.assertEqual(failures['failures'][0]['id'], 'native:[bad]')

    @unittest.skipIf(sys.platform == 'win32', 'POSIX C locale establishes an ASCII default')
    def test_ascii_default_locale_preserves_filled_marathi_and_resumes_utf8_cache(self):
        # Exercise actual disk reads/writes in a fresh non-UTF8 process, rather
        # than mocking Path.read_text or assuming the test runner's locale.
        script = r"""
import codecs, importlib.util, json, locale, pathlib, sys
spec = importlib.util.spec_from_file_location('draft', sys.argv[1])
draft = importlib.util.module_from_spec(spec)
spec.loader.exec_module(draft)
assert not sys.flags.utf8_mode
if codecs.lookup(locale.getpreferredencoding(False)).name != 'ascii':
    raise SystemExit(77)
root = pathlib.Path(sys.argv[2])
paths = draft.PROGRAM.catalog_paths(root, 'mr')
paths['native'].parent.mkdir(parents=True)
filled = '\u0938\u0902\u092a\u093e\u0926\u093f\u0924'
translated = '\u0928\u092e\u0938\u094d\u0915\u093e\u0930'
paths['native'].write_text('[edited]\n' + filled + '\n', encoding='utf-8')
draft.atomic_json(paths['online'], {'Edited': filled, 'Hello': ''})
invariant = draft.PROGRAM.ROOT / 'platform/packages/i18n/invariants.json'
draft.atomic_json(root / 'manifest.json', {
    'locales': ['mr'], 'source_sha256': 'test',
    'source_files': {'platform/packages/i18n/invariants.json': draft.PROGRAM.digest(invariant.read_bytes())},
    'batches': [{'path': 'native.json'}, {'path': 'online.json'}]})
draft.atomic_json(root / 'native.json', {'surface': 'native', 'domain': 'general', 'entries': [
    {'id': 'native:[edited]', 'key': '[edited]', 'source': 'Edited'}]})
draft.atomic_json(root / 'online.json', {'surface': 'online', 'domain': 'online', 'entries': [
    {'id': 'online:Edited', 'key': 'Edited', 'source': 'Edited'},
    {'id': 'online:Hello', 'key': 'Hello', 'source': 'Hello'}]})
calls = []
def fake(project, payload):
    calls.append(payload)
    return {'translations': [{'translatedText': translated}]}
draft.run(root, 'mr', 'project', execute=True, translate=fake)
# Re-open all persistent state and re-use the UTF8 response cache after an
# explicit blank restoration; a cache miss would attempt fake dispatch again.
draft.atomic_json(paths['online'], {'Edited': filled, 'Hello': ''})
def forbidden(*args):
    raise AssertionError('Cache resume must not dispatch another request')
draft.run(root, 'mr', 'project', execute=True, translate=forbidden)
assert len(calls) == 1
assert paths['native'].read_text(encoding='utf-8') == '[edited]\n' + filled + '\n'
assert json.loads(paths['online'].read_text(encoding='utf-8')) == {'Edited': filled, 'Hello': translated}
assert filled.encode('utf-8') in paths['online'].read_bytes()
assert len(json.loads((root / 'google-budget.json').read_text(encoding='utf-8'))['attempts']) == 1
# Exercise the independent HTML-plan cache under the same ASCII locale too.
class Client:
    project = 'project'
    location = 'global'
    calls = 0
    def call(self, method, payload):
        self.calls += 1
        return {'translations': [{'translatedText': translated}]}
client = Client()
plan = {'locale': 'nb', 'source_sha256': 'frozen', 'chunks': [['Hello']],
        'entries': [{'surface': 'native', 'key': '[hello]', 'variant': '', 'source': 'Hello'}]}
run = draft.translate(plan, root, client, 'nb', 10000)
assert json.loads((run / 'native.json').read_text(encoding='utf-8')) == {'[hello]': translated}
draft.translate(plan, root, client, 'nb', 1)
assert client.calls == 1
assert len(json.loads((root / 'google-budget.json').read_text(encoding='utf-8'))['attempts']) == 2
"""
        environment = dict(os.environ, PYTHONUTF8='0', PYTHONCOERCECLOCALE='0', LC_ALL='C')
        result = subprocess.run([sys.executable, '-c', script, str(SPEC.origin), str(self.root)],
                                env=environment, capture_output=True, text=True, encoding='utf-8')
        if result.returncode == 77:
            self.skipTest('This platform keeps a non-ASCII encoding in the C locale')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_dry_run_is_unpaid_and_apply_preserves_editorial_text_and_plurals(self):
        paths = DRAFT.PROGRAM.catalog_paths(self.root, 'mr')
        paths['native'].parent.mkdir(parents=True)
        paths['native'].write_text('[hello]\n\n[edited]\nसंपादित\n', encoding='utf-8')
        DRAFT.atomic_json(paths['online'], {'{count} item': {'one': '', 'other': ''}, ' ': '', 'Paragraphs': ''})
        invariant = DRAFT.PROGRAM.ROOT / 'platform/packages/i18n/invariants.json'
        DRAFT.atomic_json(self.root / 'manifest.json', {
            'locales': ['mr'], 'source_sha256': 'frozen-test-source', 'source_files': {'platform/packages/i18n/invariants.json':
            DRAFT.PROGRAM.digest(invariant.read_bytes())},
            'batches': [{'path': 'native.json'}, {'path': 'online.json'}]})
        DRAFT.atomic_json(self.root / 'native.json', {'surface': 'native', 'domain': 'general', 'entries': [
            {'id': 'native:[hello]', 'key': '[hello]', 'source': 'Hello'},
            {'id': 'native:[edited]', 'key': '[edited]', 'source': 'Edited'}]})
        DRAFT.atomic_json(self.root / 'online.json', {'surface': 'online', 'domain': 'online', 'entries': [
            {'id': 'online:{count} item', 'key': '{count} item',
             'source': {'one': '{count} item', 'other': '{count} items'}},
            {'id': 'online: ', 'key': ' ', 'source': ' '},
            {'id': 'online:Paragraphs', 'key': 'Paragraphs', 'source': 'Hello\\n\\nworld\nagain'}]})
        calls = []

        def translate(project, payload):
            calls.append(payload)
            self.assertTrue(all('\n' not in text and '\\n' not in text for text in payload['contents']))
            return {'translations': [{'translatedText': text.replace('Hello', 'नमस्कार').replace('items', 'वस्तू').replace('item', 'वस्तू')}
                                     for text in payload['contents']]}

        summary = DRAFT.run(self.root, 'mr', 'project', translate=translate)
        self.assertTrue(summary['dry_run'])
        self.assertEqual(calls, [])
        self.assertFalse((self.root / 'google-budget.json').exists())
        self.assertEqual(summary['pending_variants'], 4)
        DRAFT.run(self.root, 'mr', 'project', execute=True, translate=translate)
        native, errors = DRAFT.PROGRAM.CHECKER.read_table(paths['native'])
        self.assertEqual(errors, [])
        self.assertEqual(native, {'[hello]': 'नमस्कार', '[edited]': 'संपादित'})
        online = json.loads(paths['online'].read_text())
        self.assertEqual(online['{count} item'], {'one': '{count} वस्तू', 'other': '{count} वस्तू'})
        self.assertEqual(online[' '], ' ')
        self.assertEqual(online['Paragraphs'], 'नमस्कार\\n\\nworld\nagain')
        run = json.loads(next((self.root / 'google-runs').glob('*.json')).read_text())
        self.assertEqual(run['source_sha256'], 'frozen-test-source')
        self.assertEqual(len(run['entries']), 4)
        DRAFT.run(self.root, 'mr', 'project', execute=True, translate=translate)
        self.assertEqual(len(calls), 1)




class FakeClient:
    project = 'test-project'
    location = 'global'

    def __init__(self):
        self.calls = []

    def call(self, method, payload):
        self.calls.append((method, payload))
        return {'translations': [{'translatedText': value.replace('Hello', 'Hei')}
                                 for value in payload['contents']]}


class GooglePlanDraftTest(unittest.TestCase):
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



class GoogleMergedApiTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.client = FakeClient()
        self.plan = {'locale': 'nb', 'source_sha256': 'frozen',
                     'chunks': [['Hello']], 'entries': [
                         {'surface': 'native', 'key': '[hello]', 'variant': '', 'source': 'Hello'}]}

    def test_plan_and_blank_fill_share_existing_campaign_usd_ceiling(self):
        budget = DRAFT.Budget(self.output / 'google-budget.json', self.client.project, '0.0001')
        DRAFT.fetch(self.client.project, {'contents': ['abcd']}, self.output / 'google-responses',
                    budget, lambda *_: {'translations': [{'translatedText': 'old'}]})
        with self.assertRaisesRegex(ValueError, 'exceed'):
            DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000, ceiling='0.0001')
        self.assertEqual(self.client.calls, [])
        ledger = json.loads(budget.path.read_text(encoding='utf-8'))
        self.assertEqual(sum(a['characters'] for a in ledger['attempts']), 4)

    def test_existing_upstream_paid_cache_and_attempts_adopt_once_without_network(self):
        payload = {'sourceLanguageCode': 'en', 'targetLanguageCode': 'nb',
                   'mimeType': 'text/html', 'contents': ['Hello']}
        folder = self.output / 'google/nb'
        key = DRAFT.cache_key(payload, self.client.project, self.client.location)
        DRAFT.atomic_json(folder / 'attempts/old.json', {
            'request_sha256': key, 'characters': 5, 'project': self.client.project,
            'location': self.client.location, 'attempt': 1})
        cached = folder / 'responses' / (key + '.json')
        DRAFT.atomic_json(cached, {'request': payload, 'project': self.client.project,
            'location': self.client.location, 'response': {'translations': [{'translatedText': 'Hei'}]}})
        previous = cached.read_bytes()
        for _ in range(2):
            DRAFT.translate(self.plan, self.output, self.client, 'nb', 1)
        self.assertEqual(self.client.calls, [])
        self.assertEqual(cached.read_bytes(), previous)
        ledger = json.loads((self.output / 'google-budget.json').read_text(encoding='utf-8'))
        self.assertEqual(len(ledger['attempts']), 1)
        self.assertEqual(ledger['attempts'][0]['characters'], 5)

    def test_shared_kit_lock_blocks_both_apis_before_dispatch_or_catalog_reads(self):
        with DRAFT.kit_lock(self.output):
            with self.assertRaises(OSError):
                DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000)
            with self.assertRaises(OSError):
                DRAFT.run(self.output, 'mr', self.client.project, execute=True)
        self.assertEqual(self.client.calls, [])
        self.assertFalse((self.output / 'google-budget.json').exists())

    def test_html_physical_control_contract_fails_before_paid_dispatch(self):
        for source in ('Hello\nworld', 'Hello\rworld', 'Hello\tworld'):
            with self.subTest(source=source):
                with self.assertRaisesRegex(ValueError, 'blank-fill'):
                    DRAFT.protect(source)
                with self.assertRaisesRegex(ValueError, 'blank-fill'):
                    DRAFT.restore(source, 'Hei world')
                plan = dict(self.plan, chunks=[[source]])
                with self.assertRaisesRegex(ValueError, 'blank-fill'):
                    DRAFT.translate(plan, self.output, self.client, 'nb', 10000)
        self.assertEqual(self.client.calls, [])
        self.assertFalse((self.output / 'google-budget.json').exists())

    def test_stale_low_level_budget_instances_cannot_overspend_shared_ledger(self):
        path = self.output / 'google-budget.json'
        first = DRAFT.Budget(path, self.client.project, '0.0001')
        stale = DRAFT.Budget(path, self.client.project, '0.0001')
        DRAFT.fetch(self.client.project, {'contents': ['hello']}, self.output / 'cache', first,
                    lambda *_: {'translations': [{'translatedText': 'Hei'}]})
        calls = []
        with self.assertRaisesRegex(ValueError, 'exceed'):
            DRAFT.fetch(self.client.project, {'contents': ['again']}, self.output / 'cache', stale,
                        lambda *args: calls.append(args))
        self.assertEqual(calls, [])
        self.assertEqual(len(json.loads(path.read_text(encoding='utf-8'))['attempts']), 1)

    def test_plan_directory_fsync_failure_prevents_network(self):
        with patch.object(DRAFT, 'fsync_directory', side_effect=OSError('no durable directory')):
            with self.assertRaisesRegex(OSError, 'no durable directory'):
                DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000)
        self.assertEqual(self.client.calls, [])

    def test_auth_retry_reserves_before_each_post_and_obeys_cap(self):
        client = DRAFT.GoogleClient('test-project')
        with patch.object(DRAFT.subprocess, 'check_output', return_value='not-a-real-token'):
            with patch.object(DRAFT, 'urlopen', side_effect=HTTPError('url', 401, 'expired', {}, io.BytesIO())) as network:
                with self.assertRaisesRegex(ValueError, 'budget exhausted'):
                    DRAFT.translate(self.plan, self.output, client, 'nb', 5)
        self.assertEqual(network.call_count, 1)
        ledger = json.loads((self.output / 'google-budget.json').read_text(encoding='utf-8'))
        self.assertEqual(len(ledger['attempts']), 1)
        self.assertEqual(ledger['attempts'][0]['state'], 'dispatched_or_uncertain')

    def test_new_plan_cache_hash_detects_mutation_without_dispatch(self):
        DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000)
        cached = next((self.output / 'google/nb/responses').glob('*.json'))
        record = json.loads(cached.read_text(encoding='utf-8'))
        record['response']['translations'][0]['translatedText'] = 'tampered but token-valid'
        DRAFT.atomic_json(cached, record)
        with self.assertRaisesRegex(ValueError, 'Corrupt'):
            DRAFT.translate(self.plan, self.output, self.client, 'nb', 10000)
        self.assertEqual(len(self.client.calls), 1)


if __name__ == '__main__':
    unittest.main()
