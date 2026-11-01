#!/usr/bin/env python3
"""Paid calls must remain bounded, resumable and safe for format contracts."""
import importlib.util
import json
import os
import subprocess
import sys
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


if __name__ == '__main__':
    unittest.main()
