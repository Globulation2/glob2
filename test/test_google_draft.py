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
from urllib.error import HTTPError

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


class TechnicalPathTest(unittest.TestCase):
    def test_relative_routes_and_files_remain_literal_in_drafts(self):
        source = 'Open /ais/favourites or data/fonts/sans.ttf at https://example.com/a/b.'
        protection = DRAFT.protect(source)
        self.assertIn('/ais/favourites', protection[1])
        self.assertIn('data/fonts/sans.ttf', protection[1])
        self.assertEqual(DRAFT.restore(protection[0], protection, source), source)


class OnlinePercentTest(unittest.TestCase):
    def test_online_percentages_do_not_use_native_stringtable_percent_grammar(self):
        source = '95%: {slot0}'
        protection = DRAFT.protect(source)
        provider = protection[0].replace('95%', '95% ')
        self.assertEqual(DRAFT.restore(provider, protection, source, native=False), '95% : {slot0}')
        source = '{value0}% saturation'
        protection = DRAFT.protect(source)
        provider = protection[0].replace(' saturation', 'ความอิ่มตัว')
        self.assertTrue(DRAFT.restore(provider, protection, source, native=False).endswith('%ความอิ่มตัว'))


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
        result = DRAFT.plan_restore(source, DRAFT.plan_protect(source).replace('Hello', 'Hei'))
        self.assertEqual(result, source.replace('Hello', 'Hei'))
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.plan_restore(source, DRAFT.plan_protect(source).replace('%0', '%1'))
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.plan_restore('Hello {name}', 'Hei')
        self.assertEqual(DRAFT.plan_restore(' / ', '/'), ' / ')
        self.assertEqual(DRAFT.plan_restore('', ''), '')

    def test_bare_protocol_allows_norwegian_compound_suffix_without_becoming_url(self):
        source = 'Enter an https:// address.'
        self.assertEqual(DRAFT.plan_restore(source, 'Skriv inn en https://-adresse.'),
                         'Skriv inn en https://-adresse.')
        # Bare scheme payload is unchanged, preserving previously paid cache keys.
        self.assertEqual(DRAFT.plan_protect(source), source)
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.plan_restore(source, 'Skriv inn en http://-adresse.')
        with self.assertRaisesRegex(ValueError, 'protected tokens'):
            DRAFT.plan_restore('Open https://globulation2.org/help',
                          'Åpne https://globulation2.org/hjelp')
        self.assertEqual(DRAFT.plan_restore('Open https://[::1]/help',
                                      'Åpne https://[::1]/help'), 'Åpne https://[::1]/help')

    def test_relative_routes_and_bare_web_addresses_cannot_be_translated(self):
        for source, altered in [('/ais/favourites', '/ais/favoritter'),
                                ('www.globulation2.org', 'www.globulation2.no'),
                                ('/play/?mode=serial', '/spill/?mode=serial')]:
            with self.subTest(source=source):
                protected = DRAFT.plan_protect(source)
                self.assertIn('translate="no"', protected)
                self.assertEqual(DRAFT.plan_restore(source, protected), source)
                with self.assertRaisesRegex(ValueError, 'protected tokens'):
                    DRAFT.plan_restore(source, altered)
        self.assertEqual(DRAFT.plan_protect('front/back application/octet-stream'),
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
        size = sum(len(DRAFT.plan_protect(text)) for text in self.plan['chunks'][0])
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



class WorkflowCompatibilityTest(unittest.TestCase):
    def test_cli_keeps_both_existing_workflows_and_does_not_pay_during_dispatch(self):
        for argv, selected in [(['--budget-usd', '25'], 'campaign_main'),
                               (['--budget-usd=25'], 'campaign_main'),
                               (['--locale', 'nb'], 'plan_main')]:
            with self.subTest(argv=argv), patch.object(DRAFT.sys, 'argv', ['google_draft.py', *argv]):
                with patch.object(DRAFT, 'campaign_main', return_value=0) as campaign:
                    with patch.object(DRAFT, 'plan_main', return_value=0) as planned:
                        self.assertEqual(DRAFT.main(), 0)
                        self.assertEqual(campaign.call_count, int(selected == 'campaign_main'))
                        self.assertEqual(planned.call_count, int(selected == 'plan_main'))

    def test_plan_writer_uses_the_same_exclusive_campaign_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            client = FakeClient()
            with DRAFT.campaign_lock(output / 'google' / 'nb'):
                with self.assertRaisesRegex(ValueError, 'locked'):
                    DRAFT.translate({'locale': 'nb'}, output, client, 'nb', 100)
            self.assertEqual(client.calls, [])

if __name__ == '__main__':
    unittest.main()
