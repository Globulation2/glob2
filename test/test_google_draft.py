#!/usr/bin/env python3
"""Google drafting remains resumable, contract-safe, and bounded without API calls."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import io
from contextlib import redirect_stdout
import unittest
from unittest.mock import patch
import urllib.error

SPEC = importlib.util.spec_from_file_location('google_draft',
    Path(__file__).resolve().parents[1] / 'tools/localization/google_draft.py')
DRAFT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRAFT)


class ProtectionTest(unittest.TestCase):
    def test_restore_tokens_invariants_and_whitespace_after_reordering(self):
        source = '  Set {slot0} mapId to %1. '
        protection = DRAFT.protect(source, ['mapId'])
        # A provider may reorder spans and alter their enclosed text. IDs, rather
        # than the returned enclosed spelling, establish literal identity.
        result = DRAFT.restore('ตั้ง <span id="g2p2">changed</span> '
                               '<span id="g2p1">changed</span> <span id="g2p0">changed</span>',
                               protection, source)
        self.assertEqual(result, '  ตั้ง %1 mapId {slot0} ')

    def test_reject_lost_duplicated_and_unknown_spans(self):
        source = 'Value {count}'
        protection = DRAFT.protect(source)
        for value in ('ค่า', '<span id="g2p0">x</span><span id="g2p0">x</span>',
                      '<span id="g2p9">x</span>'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                DRAFT.restore(value, protection, source)

    def test_no_double_html_decode(self):
        protection = DRAFT.protect('This & that')
        self.assertEqual(DRAFT.restore('นี่ &amp; นั่น', protection, 'This & that'), 'นี่ & นั่น')

    def test_profile_paragraph_separators_and_hotkeys_remain_literal(self):
        source = r'Core strategy: Wait.\n\nStrengths: Idle [R] Ctrl+Enter'
        protection = DRAFT.protect(source)
        self.assertEqual(protection[1].count(':'), 2)
        self.assertEqual(protection[1].count(r'\n'), 2)
        self.assertIn('[R]', protection[1])
        self.assertIn('Ctrl+Enter', protection[1])
        self.assertEqual(DRAFT.restore(protection[0], protection, source), source)

    def test_urls_markup_and_product_names_remain_literal(self):
        source = '<b>Glob2</b> https://example.com/a?b=1&c=2'
        protection = DRAFT.protect(source)
        self.assertIn('https://example.com/a?b=1&c=2', protection[1])
        self.assertEqual(DRAFT.restore(protection[0], protection, source), source)

    def test_invalid_characters_and_new_placeholder_rejected(self):
        for value in ('ค่า\nใหม่', 'ค่า\ufffd', 'ค่า {new}', ''):
            with self.subTest(value=value), self.assertRaises(ValueError):
                DRAFT.restore(value, DRAFT.protect('Value'), 'Value')


class CampaignTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.campaign = self.create()

    def create(self, budget='25', source='source'):
        return DRAFT.Campaign(self.folder, 'test-project', 'th', source, budget, '20')

    def test_lock_rejects_second_writer_and_releases_after_exception(self):
        with self.assertRaisesRegex(RuntimeError, 'intentional'):
            with DRAFT.campaign_lock(self.folder):
                with self.assertRaises(ValueError):
                    with DRAFT.campaign_lock(self.folder):
                        self.fail('Second writer acquired campaign lock')
                raise RuntimeError('intentional')
        self.assertFalse((self.folder / 'campaign.lock').exists())
        with DRAFT.campaign_lock(self.folder):
            self.assertTrue((self.folder / 'campaign.lock').exists())

    def test_nonfinite_budget_or_rate_is_rejected(self):
        for budget, rate in [('NaN', '20'), ('Infinity', '20'), ('25', 'Infinity')]:
            with self.subTest(budget=budget, rate=rate), self.assertRaises(ValueError):
                DRAFT.Campaign(self.folder, 'test-project', 'th', 'source', budget, rate)

    def test_cache_hit_never_calls_provider_or_charges_again(self):
        response = {'translations': [{'translatedText': 'ค่า'}]}
        with patch.object(self.campaign, 'send', return_value=response) as send:
            self.assertEqual(self.campaign.translate(['Value']), response)
            self.assertEqual(self.campaign.translate(['Value']), response)
        send.assert_called_once()
        fresh = self.create()
        with patch.object(fresh, 'send', side_effect=AssertionError('network')):
            self.assertEqual(fresh.translate(['Value']), response)
        self.assertEqual(len(fresh.ledger['attempts']), 1)
        self.assertEqual(str(fresh.spent), '0.0001')
        cache = next((self.folder / 'requests').glob('*.json'))
        self.assertEqual(json.loads(cache.read_text())['response'], response)

    def test_reservation_is_durable_before_network_and_blocks_overspend(self):
        self.campaign.budget = DRAFT.Decimal('0.0001')
        def provider(request):
            saved = json.loads(self.campaign.path.read_text())
            self.assertEqual(saved['attempts'][0]['state'], 'reserved')
            return {'translations': [{'translatedText': 'ค่า'}]}
        with patch.object(self.campaign, 'send', side_effect=provider):
            self.campaign.translate(['Value'])
        with patch.object(self.campaign, 'send') as send, self.assertRaises(ValueError):
            self.campaign.translate(['Other'])
        send.assert_not_called()

    def test_ambiguous_failures_and_retries_all_count_toward_budget(self):
        with patch.object(self.campaign, 'send', side_effect=urllib.error.URLError('lost')), \
                patch.object(DRAFT.time, 'sleep'), self.assertRaises(RuntimeError):
            self.campaign.translate(['Value'])
        fresh = self.create()
        self.assertEqual(len(fresh.ledger['attempts']), 3)
        self.assertEqual(str(fresh.spent), '0.0003')
        self.assertTrue(all(a['state'] == 'ambiguous_network_error' for a in fresh.ledger['attempts']))

    def test_no_retry_permanent_http_error_and_no_response_credentials(self):
        error = urllib.error.HTTPError('url', 403, 'denied', {}, None)
        with patch.object(self.campaign, 'send', side_effect=error) as send, self.assertRaises(RuntimeError):
            self.campaign.translate(['Value'])
        self.assertEqual(send.call_count, 1)
        self.assertNotIn('Authorization', self.campaign.path.read_text())

    def test_campaign_rejects_changed_source_project_or_locale(self):
        self.campaign.reserve('request', 1)
        with self.assertRaises(ValueError):
            self.create(source='changed')
        with self.assertRaises(ValueError):
            DRAFT.Campaign(self.folder, 'other-project', 'th', 'source', '25', '20')

    def test_corrupted_cache_identity_is_rejected(self):
        with patch.object(self.campaign, 'send', return_value={'translations': []}):
            self.campaign.translate(['Value'])
        cache = next((self.folder / 'requests').glob('*.json'))
        payload = json.loads(cache.read_text())
        payload['request']['contents'] = ['Wrong']
        cache.write_text(json.dumps(payload))
        with self.assertRaises(ValueError):
            self.campaign.translate(['Value'])

    def test_nonempty_reviewed_values_and_optional_blank_source_are_skipped(self):
        entries = DRAFT.pending_entries({'[a]': 'Hello', '[b]': '', '[c]': 'Other'},
            {'Items': {'one': 'Item', 'other': '{count} items'}, 'Hello': 'Hello'},
            {'[a]': 'สวัสดี', '[b]': '', '[c]': ''},
            {'Items': {'other': ''}, 'Hello': 'สวัสดี'}, {})
        self.assertEqual(entries, [('native', '[c]', None, 'Other', ()),
                                   ('online', 'Items', 'other', '{count} items', ())])


class DraftIntegrationTest(unittest.TestCase):
    def test_selected_pilot_preserves_reviewed_work_and_resumes_without_spend(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            source = output / 'source'
            source.mkdir()
            native = {'[new]': 'Hello %0', '[reviewed]': 'Reviewed', '[blank]': ''}
            online = {'Hello': 'Hello', '{count} items': {'one': '{count} item', 'other': '{count} items'}}
            (output / 'manifest.json').write_text(json.dumps({'source_sha256': 'source', 'locales': ['th']}))
            (source / 'native.json').write_text(json.dumps(native))
            (source / 'online.json').write_text(json.dumps(online))
            folder = output / 'drafts/th'
            folder.mkdir(parents=True)
            (folder / 'texts.th.txt').write_text('[new]\n\n[reviewed]\nตรวจแล้ว\n[blank]\n\n')
            (folder / 'th.json').write_text(json.dumps({'Hello': '', '{count} items': {'other': ''}}))
            ids = output / 'pilot.json'
            ids.write_text(json.dumps(['native:[new]', 'online:{count} items']))
            args = SimpleNamespace(output=output, project='test-project', locale='th', budget_usd='25',
                                   usd_per_million='20', limit=None, ids_file=ids)
            def response(request):
                return {'translations': [{'translatedText': text.replace('Hello', 'สวัสดี').replace('items', 'รายการ')}
                                         for text in request['contents']]}
            with patch.object(DRAFT.PROGRAM, 'sources', return_value=(list(native), native, online, {}, 'source')), \
                    patch.object(DRAFT.Campaign, 'send', side_effect=response) as send, redirect_stdout(io.StringIO()):
                result = DRAFT.run(args)
                resumed = DRAFT.run(args)
            self.assertEqual(send.call_count, 1)
            self.assertEqual(result['translated_this_run'], 2)
            self.assertEqual(resumed['translated_this_run'], 0)
            self.assertEqual(result['reserved_usd'], resumed['reserved_usd'])
            self.assertIn('[reviewed]\nตรวจแล้ว\n', (folder / 'texts.th.txt').read_text())
            target = json.loads((folder / 'th.json').read_text())
            self.assertEqual(target['Hello'], '')
            self.assertEqual(target['{count} items'], {'other': '{count} รายการ'})
            self.assertTrue((output / 'google/th/check-draft.json').is_file())
            self.assertFalse((output / 'google/th/campaign.lock').exists())


if __name__ == '__main__':
    unittest.main()
