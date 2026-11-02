#!/usr/bin/env python3
"""Exercise cloud budgeting, recovery and token safety without API credentials."""
from decimal import Decimal
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

TOOLS = Path(__file__).resolve().parents[1] / 'tools/localization'
sys.path.insert(0, str(TOOLS))
try:
    SPEC = importlib.util.spec_from_file_location('cloud_draft', TOOLS / 'cloud_draft.py')
    CLOUD = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(CLOUD)
finally:
    sys.path.pop(0)


class FakeCloud:
    def __init__(self, failure=None):
        self.calls = []
        self.failure = failure

    def translate(self, contents):
        self.calls.append(contents)
        if self.failure:
            failure, self.failure = self.failure, None
            raise failure
        return {'translations': [{'translatedText': 'বাংলা ' + text} for text in contents]}


class CloudDraftTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / 'kit'
        for name in CLOUD.program.SOURCE_PATHS:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('{}', encoding='utf-8')
        (self.root / 'content/data/texts.keys.txt').write_text('[hello]\n[value %0]\n', encoding='utf-8')
        (self.root / 'content/data/texts.en.txt').write_text('[hello]\nHello\n[value %0]\nValue %0\n', encoding='utf-8')
        (self.root / 'services/online/packages/i18n/locales/en.json').write_text(json.dumps({
            'Hello': 'Hello', '{count} items': {'one': '{count} item', 'other': '{count} items'}}))
        with patch.object(CLOUD.program.subprocess, 'check_output', return_value='revision'):
            CLOUD.program.prepare(self.output, root=self.root, locales=('bn',))
        self.plurals = patch.object(CLOUD.program, 'plural_categories', return_value=['one', 'other'])
        self.plurals.start()
        self.addCleanup(self.plurals.stop)
        CLOUD.program.draft(self.output, 'bn')
        self.cloud = FakeCloud()

    def run_draft(self, **kwargs):
        return CLOUD.run(self.output, 'bn', 'test-project', cloud=self.cloud,
                         phase='bulk', pilot_reviewed=True, sleep=lambda _: None, **kwargs)

    def test_protected_tokens_roundtrip_and_damage_rejected(self):
        source = r'Use %0 {count} <b>AI</b> https://example.org/a.json foo.js data/x.txt \n %s %%'
        protected, tokens = CLOUD.protect(source)
        self.assertNotIn('%0', protected)
        self.assertNotIn('foo.js', protected)
        self.assertEqual(CLOUD.restore(protected, tokens), source)
        with self.assertRaises(ValueError):
            CLOUD.restore(protected.replace('GLOB2KEEP0END', ''), tokens)
        with self.assertRaises(ValueError):
            CLOUD.restore(protected + 'GLOB2KEEP0END', tokens)
        with self.assertRaises(ValueError):
            CLOUD.restore(protected + 'GLOB2KEEP999END', tokens)

    def test_dry_run_does_not_authenticate_or_fill_catalog(self):
        summary = self.run_draft()
        self.assertGreater(summary['new_characters'], 0)
        self.assertEqual(self.cloud.calls, [])
        self.assertFalse((self.output / 'cloud/bn/accounting.json').exists())
        self.assertEqual(len(CLOUD.program.check_draft(self.output, 'bn')['untranslated']), 5)

    def test_cached_duplicate_source_and_resume_preserve_edits(self):
        self.run_draft(execute=True)
        sent = [text for call in self.cloud.calls for text in call]
        self.assertEqual(sent.count('Hello'), 1)
        paths = CLOUD.program.catalog_paths(self.output, 'bn')
        online = json.loads(paths['online'].read_text())
        online['Hello'] = 'সম্পাদিত'
        online['{count} items']['one'] = ''
        paths['online'].write_text(json.dumps(online, ensure_ascii=False))
        call_count = len(self.cloud.calls)
        self.run_draft(execute=True)
        self.assertEqual(len(self.cloud.calls), call_count)
        final = json.loads(paths['online'].read_text())
        self.assertEqual(final['Hello'], 'সম্পাদিত')
        self.assertTrue(final['{count} items']['one'])
        self.assertEqual(CLOUD.program.check_draft(self.output, 'bn')['errors'], [])

    def test_preflight_cap_rejects_before_any_paid_request(self):
        with self.assertRaisesRegex(ValueError, 'spending ceiling'):
            self.run_draft(execute=True, cap=Decimal('0.000001'))
        self.assertEqual(self.cloud.calls, [])

    def test_edit_during_request_is_not_overwritten(self):
        paths = CLOUD.program.catalog_paths(self.output, 'bn')
        original_translate = self.cloud.translate

        def translate(contents):
            values = json.loads(paths['online'].read_text())
            values['Hello'] = 'পর্যালোচিত'
            paths['online'].write_text(json.dumps(values, ensure_ascii=False))
            return original_translate(contents)

        with patch.object(self.cloud, 'translate', side_effect=translate):
            self.run_draft(execute=True)
        values = json.loads(paths['online'].read_text())
        self.assertEqual(values['Hello'], 'পর্যালোচিত')

    def test_timeout_reservation_survives_retry(self):
        self.cloud.failure = TimeoutError()
        result = self.run_draft(execute=True)
        ledger = json.loads((self.output / 'cloud/bn/accounting.json').read_text())
        self.assertEqual(len(ledger['attempts']), 2)
        self.assertEqual(ledger['reserved_characters'], 2 * sum(map(len, self.cloud.calls[0])))
        self.assertEqual(result['reserved_characters'], ledger['reserved_characters'])
        self.assertEqual(ledger['attempts'][0]['status'], 'transport-error')

    def test_retry_cannot_exceed_cap(self):
        estimate = self.run_draft()
        cap = Decimal(estimate['estimated_total_usd_without_free_credit'])
        self.cloud.failure = TimeoutError()
        with self.assertRaisesRegex(ValueError, 'spending ceiling'):
            self.run_draft(execute=True, cap=cap)
        self.assertEqual(len(self.cloud.calls), 1)
        ledger = json.loads((self.output / 'cloud/bn/accounting.json').read_text())
        self.assertEqual(ledger['reserved_characters'], sum(map(len, self.cloud.calls[0])))

    def test_nonretryable_http_error_is_sanitized(self):
        self.cloud.failure = urllib.error.HTTPError('secret-url', 403, 'private detail', {}, None)
        with self.assertRaisesRegex(ValueError, '^Translation request failed; see accounting status$'):
            self.run_draft(execute=True)
        self.assertEqual(len(self.cloud.calls), 1)

    def test_bulk_requires_pilot_and_changed_project_cannot_reset_budget(self):
        with self.assertRaisesRegex(ValueError, 'pilot'):
            CLOUD.run(self.output, 'bn', 'test-project', phase='bulk')
        self.run_draft(execute=True)
        with self.assertRaisesRegex(ValueError, 'Accounting configuration changed'):
            CLOUD.run(self.output, 'bn', 'other-project')

    def test_damaged_response_is_retained_without_filling_drafts(self):
        with patch.object(self.cloud, 'translate', return_value={'translations': []}):
            with self.assertRaisesRegex(ValueError, 'response count'):
                self.run_draft(execute=True)
        self.assertTrue((self.output / 'cloud/bn/responses/00001.json').exists())
        self.assertEqual(json.loads((self.output / 'cloud/bn/cache.json').read_text()), {})

    def test_formatting_only_values_are_not_sent_to_api(self):
        self.assertFalse(CLOUD.needs_translation('(%0) / {count}'))
        self.assertFalse(CLOUD.needs_translation('data/game.js'))
        self.assertTrue(CLOUD.needs_translation('Value %0'))

    def test_partial_token_damage_caches_good_items_and_blocks_rebilling(self):
        def damage(contents):
            return {'translations': [{'translatedText': text.replace('GLOB2KEEP0END', 'ক্ষতি')}
                                     for text in contents]}

        with patch.object(self.cloud, 'translate', side_effect=damage) as call:
            with self.assertRaisesRegex(ValueError, 'manual draft correction'):
                self.run_draft(execute=True)
            self.assertEqual(call.call_count, 1)
            with self.assertRaisesRegex(ValueError, 'manual draft correction'):
                self.run_draft(execute=True)
            self.assertEqual(call.call_count, 1)
        cache = json.loads((self.output / 'cloud/bn/cache.json').read_text())
        self.assertIn('Hello', cache)
        self.assertNotIn('Value %0', cache)
        ledger = json.loads((self.output / 'cloud/bn/accounting.json').read_text())
        self.assertEqual(ledger['attempts'][0]['status'], 'completed-with-rejections')

    def test_rejection_does_not_block_later_batches_or_rebill_on_resume(self):
        def damage_one(contents):
            self.cloud.calls.append(contents)
            return {'translations': [{'translatedText': 'বাংলা ' + (
                text.replace('GLOB2KEEP0END', 'ক্ষতি') if text.startswith('Value ') else text)}
                for text in contents]}

        with patch.object(CLOUD, 'MAX_REQUEST_ITEMS', 1), patch.object(self.cloud, 'translate', side_effect=damage_one):
            with self.assertRaisesRegex(ValueError, '1 protected-token rejections require manual draft correction'):
                self.run_draft(execute=True)
            self.assertEqual(len(self.cloud.calls), 4)
            cache = json.loads((self.output / 'cloud/bn/cache.json').read_text())
            # Plural requests followed the rejected native request and still
            # completed; the failed source alone requires manual correction.
            self.assertIn('{count} item', cache)
            self.assertIn('{count} items', cache)
            self.assertNotIn('Value %0', cache)
            with self.assertRaisesRegex(ValueError, 'manual draft correction'):
                self.run_draft(execute=True)
            self.assertEqual(len(self.cloud.calls), 4)

    def test_plural_categories_are_resolved_once_per_inventory(self):
        source_path = self.output / 'source/online.json'
        source = json.loads(source_path.read_text())
        source['{count} other things'] = {'one': '{count} thing', 'other': '{count} things'}
        source_path.write_text(json.dumps(source))
        manifest = json.loads((self.output / 'manifest.json').read_text())
        batch_spec = next(batch for batch in manifest['batches'] if batch['id'].startswith('online-'))
        batch_path = self.output / batch_spec['path']
        batch = json.loads(batch_path.read_text())
        batch['entries'].append({'id': 'online:{count} other things', 'key': '{count} other things',
                                 'source': source['{count} other things']})
        batch_path.write_text(json.dumps(batch))
        path = CLOUD.program.catalog_paths(self.output, 'bn')['online']
        target = json.loads(path.read_text())
        target['{count} other things'] = {'one': '', 'other': ''}
        path.write_text(json.dumps(target))
        with patch.object(CLOUD.program, 'plural_categories', return_value=['one', 'other']) as plural:
            self.run_draft()
        plural.assert_called_once_with('bn')

    def test_response_saved_before_interruption_recovers_without_api_call(self):
        with patch.object(CLOUD, 'accept_response', side_effect=OSError('interrupted')):
            with self.assertRaises(OSError):
                self.run_draft(execute=True)
        self.assertEqual(len(self.cloud.calls), 1)
        self.run_draft(execute=True)
        self.assertEqual(len(self.cloud.calls), 1)
        self.assertEqual(CLOUD.program.check_draft(self.output, 'bn')['untranslated'], [])

    def test_exclusive_lock_fails_closed(self):
        state = self.output / 'cloud/bn'
        state.mkdir(parents=True)
        (state / 'runner.lock').write_text('another process')
        with self.assertRaisesRegex(ValueError, 'lock exists'):
            self.run_draft(execute=True)
        self.assertEqual(self.cloud.calls, [])


if __name__ == '__main__':
    unittest.main()
