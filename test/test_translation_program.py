#!/usr/bin/env python3
"""Review provenance must become stale after edits, without inventing sign-offs."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('translation_program',
    Path(__file__).resolve().parents[1] / 'tools/localization/translation_program.py')
PROGRAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROGRAM)


class TranslationProgramTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / 'kit'
        for name in PROGRAM.SOURCE_PATHS:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('{}', encoding='utf-8')
        (self.root / PROGRAM.SOURCE_PATHS[0]).write_text('[hello]\n[value %0]\n', encoding='utf-8')
        (self.root / PROGRAM.SOURCE_PATHS[1]).write_text('[hello]\nHello\n[value %0]\nValue %0\n', encoding='utf-8')
        (self.root / PROGRAM.SOURCE_PATHS[2]).write_text(
            json.dumps({'Hello': 'Hello', '{count} items': {'one': '{count} item', 'other': '{count} items'}}),
            encoding='utf-8')
        with patch.object(PROGRAM.subprocess, 'check_output', return_value='test-revision'):
            self.manifest = PROGRAM.prepare(self.output, 1, self.root)

    def test_prepare_preserves_existing_reviews_and_refuses_changed_source(self):
        record = self.output / 'reviews/hi.json'
        record.write_text('retained evidence', encoding='utf-8')
        PROGRAM.prepare(self.output, 1, self.root)
        self.assertEqual(record.read_text(), 'retained evidence')
        (self.root / PROGRAM.SOURCE_PATHS[1]).write_text('[hello]\nChanged\n[value %0]\nValue %0\n')
        with self.assertRaises(ValueError):
            PROGRAM.prepare(self.output, 1, self.root)

    def test_batches_cover_all_keys_once_and_start_pending(self):
        keys = [key for batch in self.manifest['batches'] for key in batch['keys']]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(len(keys), 4)
        record = json.loads((self.output / 'reviews/hi.json').read_text())
        self.assertEqual(record['batches'], [])
        self.assertTrue(all(not gate['passed'] for gate in record['gates'].values()))
        self.assertTrue(PROGRAM.audit_review(self.manifest, record, {}))

    def test_selected_scope_retains_only_requested_reviews_and_cannot_be_replaced(self):
        output = self.output / 'selected'
        with patch.object(PROGRAM.subprocess, 'check_output', return_value='test-revision'):
            manifest = PROGRAM.prepare(output, 1, self.root, locales=('hi', 'ur', 'sw'))
        self.assertEqual(manifest['locales'], ['hi', 'ur', 'sw'])
        self.assertEqual({p.stem for p in (output / 'reviews').glob('*.json')}, {'hi', 'ur', 'sw'})
        with self.assertRaises(ValueError):
            PROGRAM.prepare(output, 1, self.root, locales=('hi',))

    def test_draft_is_blank_and_will_not_overwrite_work(self):
        with patch.object(PROGRAM, 'plural_categories', return_value=['one', 'other']):
            PROGRAM.draft(self.output, 'hi')
            result = PROGRAM.check_draft(self.output, 'hi')
            self.assertEqual(result['errors'], [])
            self.assertEqual(len(result['untranslated']), 5)
            with self.assertRaises(ValueError):
                PROGRAM.draft(self.output, 'hi')

    def test_draft_catches_placeholder_damage(self):
        with patch.object(PROGRAM, 'plural_categories', return_value=['one', 'other']):
            PROGRAM.draft(self.output, 'hi')
            paths = PROGRAM.catalog_paths(self.output, 'hi')
            paths['native'].write_text('[hello]\nनमस्ते\n[value %0]\nमान\n', encoding='utf-8')
            result = PROGRAM.check_draft(self.output, 'hi')
            self.assertIn('native:[value %0]: placeholder mismatch', result['errors'])

    def complete_record(self, locale='hi'):
        paths = {'native': self.root / 'native', 'online': self.root / 'online'}
        paths['native'].write_text('[hello]\nनमस्ते\n[value %0]\nमान %0\n', encoding='utf-8')
        paths['online'].write_text(json.dumps({'Hello': 'नमस्ते', '{count} items': {'one': '{count} वस्तु',
                                             'other': '{count} वस्तुएँ'}}), encoding='utf-8')
        targets = PROGRAM.batch_target_hashes(self.manifest, paths)
        hashes = {name: PROGRAM.digest(path.read_bytes()) for name, path in paths.items()}
        record = {
            'locale': locale, 'source_sha256': self.manifest['source_sha256'],
            'catalog_sha256': hashes, 'findings': [],
            'language_brief': {'variety': 'specified', 'script': 'specified', 'region': 'specified',
                               'sources': ['citation'], 'terminology_rationales': ['reason']},
            'gates': {gate: {'passed': True, 'source_sha256': self.manifest['source_sha256'],
                             'catalog_sha256': hashes, 'evidence': [{'path': str(paths['native']),
                             'sha256': hashes['native']}]} for gate in PROGRAM.GATES},
            'batches': [{
                'id': batch['id'], 'source_sha256': self.manifest['source_sha256'],
                'target_sha256': targets[batch['id']], 'author': 'agent-a', 'reviewer': 'agent-b',
                'editor': 'agent-c', 'reviewed_keys': batch['keys'], 'edited_keys': batch['keys'],
                'rechecked_keys': batch['keys'], 'rechecked_by': 'agent-b',
            } for batch in self.manifest['batches']],
        }
        return record, paths

    def test_final_edit_invalidates_review_hashes(self):
        record, paths = self.complete_record()
        self.assertEqual(PROGRAM.audit_review(self.manifest, record, paths), [])
        paths['native'].write_text('[hello]\nअन्य\n[value %0]\nमान %0\n', encoding='utf-8')
        self.assertIn('review catalog hashes are missing or stale', PROGRAM.audit_review(self.manifest, record, paths))

    def test_status_checks_actual_blank_drafts_even_with_attestations(self):
        record, _ = self.complete_record()
        with patch.object(PROGRAM, 'plural_categories', return_value=['one', 'other']):
            PROGRAM.draft(self.output, 'hi')
            issues = PROGRAM.audit_locale(self.output, self.manifest, record, 'hi')
        self.assertIn('5 untranslated draft entries', issues)

    def test_review_cannot_impersonate_another_locale_to_skip_extra_review(self):
        record, original_paths = self.complete_record('hi')
        with patch.object(PROGRAM, 'plural_categories', return_value=['one', 'other']):
            PROGRAM.draft(self.output, 'fuc-Latn')
            paths = PROGRAM.catalog_paths(self.output, 'fuc-Latn')
            for surface in paths:
                paths[surface].write_bytes(original_paths[surface].read_bytes())
            issues = PROGRAM.audit_locale(self.output, self.manifest, record, 'fuc-Latn')
        self.assertIn('review locale does not match the requested locale', issues)

    def test_english_matches_need_individual_exceptions(self):
        record, original_paths = self.complete_record()
        with patch.object(PROGRAM, 'plural_categories', return_value=['one', 'other']):
            PROGRAM.draft(self.output, 'hi')
            paths = PROGRAM.catalog_paths(self.output, 'hi')
            for surface in paths:
                paths[surface].write_bytes(original_paths[surface].read_bytes())
            paths['online'].write_text(json.dumps({'Hello': 'Hello', '{count} items': {
                'one': '{count} वस्तु', 'other': '{count} वस्तुएँ'}}), encoding='utf-8')
            issues = PROGRAM.audit_locale(self.output, self.manifest, record, 'hi')
            self.assertIn('1 English matches lack individual reviewed exceptions', issues)
            record['accepted_english'] = [{'key': 'online:Hello', 'reason': 'Explicit individual decision'}]
            issues = PROGRAM.audit_locale(self.output, self.manifest, record, 'hi')
            self.assertNotIn('1 English matches lack individual reviewed exceptions', issues)
            record['accepted_english'] = [{'key': '*', 'reason': 'Blanket acceptance'}]
            issues = PROGRAM.audit_locale(self.output, self.manifest, record, 'hi')
            self.assertTrue(any('English exceptions require exact' in issue for issue in issues))

    def test_only_edited_batch_provenance_becomes_stale(self):
        record, paths = self.complete_record()
        old = PROGRAM.batch_target_hashes(self.manifest, paths)
        paths['native'].write_text('[hello]\nअन्य\n[value %0]\nमान %0\n', encoding='utf-8')
        new = PROGRAM.batch_target_hashes(self.manifest, paths)
        changed = [key for key in old if old[key] != new[key]]
        self.assertEqual(len(changed), 1)
        issues = PROGRAM.audit_review(self.manifest, record, paths)
        stale = [issue for issue in issues if 'source/catalog revision is stale' in issue]
        self.assertEqual(stale, [changed[0] + ': source/catalog revision is stale'])

    def test_nonexistent_or_stale_evidence_cannot_pass(self):
        record, paths = self.complete_record()
        record['gates']['structural']['evidence'] = [{'path': str(self.root / 'missing'), 'sha256': 'absent'}]
        record['gates']['source_based_pilot']['catalog_sha256'] = {}
        issues = PROGRAM.audit_review(self.manifest, record, paths)
        self.assertTrue(any('evidence is missing or changed' in item for item in issues))
        self.assertTrue(any('not bound to the final' in item for item in issues))

    def test_missing_coverage_self_review_uncertainty_and_open_findings_block(self):
        record, paths = self.complete_record()
        record['batches'][0]['reviewer'] = 'agent-a'
        record['batches'][0]['reviewed_keys'] = []
        record['language_brief']['unresolved_uncertainty'] = ['unresolved meaning']
        record['findings'] = [{'key': 'native:[hello]', 'severity': 'major', 'state': 'open'}]
        issues = PROGRAM.audit_review(self.manifest, record, paths)
        self.assertTrue(any('distinct identities' in issue for issue in issues))
        self.assertTrue(any('reviewed_keys' in issue for issue in issues))
        self.assertIn('unresolved linguistic uncertainty', issues)
        self.assertIn('unresolved finding: native:[hello]', issues)

    def test_low_resource_requires_additional_independent_review(self):
        record, paths = self.complete_record('sw')
        with patch.object(PROGRAM, 'LOW_RESOURCE', {'sw'}):
            self.assertTrue(PROGRAM.audit_review(self.manifest, record, paths))
            for batch in record['batches']:
                batch['adversarial_reviewer'] = 'agent-d'
                batch['adversarial_reviewed_keys'] = batch['reviewed_keys']
            self.assertEqual(PROGRAM.audit_review(self.manifest, record, paths), [])


class MachineDraftTest(TranslationProgramTest):
    def setUp(self):
        super().setUp()
        self.auth = patch.object(PROGRAM.subprocess, 'check_output', return_value='fake-access-token\n')
        self.auth_mock = self.auth.start()
        self.addCleanup(self.auth.stop)
        self.plurals = patch.object(PROGRAM, 'plural_categories', return_value=['one', 'other'])
        self.plurals.start()
        self.addCleanup(self.plurals.stop)
        self.calls = []

    def response(self, request, timeout):
        from io import BytesIO
        payload = json.loads(request.data)
        self.calls.append(payload)
        self.assertEqual(timeout, 90)
        self.assertEqual(request.get_header('Authorization'), 'Bearer fake-access-token')
        self.assertEqual(payload['sourceLanguageCode'], 'en')
        self.assertEqual(payload['targetLanguageCode'], 'bg')
        self.assertLessEqual(sum(map(len, payload['contents'])), 5000)
        return BytesIO(json.dumps({'translations': [{'translatedText': 'БГ ' + text}
                                                   for text in payload['contents']]}).encode())

    def run_machine(self, **kwargs):
        return PROGRAM.machine_draft(self.output, 'bg', 'test-project', **kwargs)

    def test_machine_ordering_native_plural_and_authentication(self):
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=self.response):
            result = self.run_machine()
        self.assertEqual(result['filled'], 5)
        self.auth_mock.assert_called_once_with(['gcloud', 'auth', 'print-access-token'], text=True)
        paths = PROGRAM.catalog_paths(self.output, 'bg')
        native, errors = PROGRAM.CHECKER.read_table(paths['native'])
        self.assertEqual(errors, [])
        self.assertEqual(native, {'[hello]': 'БГ Hello', '[value %0]': 'БГ Value %0'})
        online = json.loads(paths['online'].read_text())
        self.assertEqual(online['{count} items'], {'one': 'БГ {count} item', 'other': 'БГ {count} items'})
        self.assertEqual(PROGRAM.check_draft(self.output, 'bg')['errors'], [])
        self.assertEqual(len(list((self.output / 'machine/bg/raw').glob('*.response.bin'))), 1)
        self.assertEqual(len(json.loads((self.output / 'machine/bg/provider-drafts.json').read_text())), 5)

    def test_protects_tokens_urls_markup_identifiers_unicode_and_invariants(self):
        original = 'Use %0 {slot0} <b>mapId</b> https://example.org/a?b=c U+1234 params.teams gameSprite Ctrl+Enter me'
        protected, replacements = PROGRAM.protect_text(original, ['Ctrl+Enter', 'me'])
        for token in ('%0', '{slot0}', '<b>', '</b>', 'mapId', 'https://example.org/a?b=c',
                      'U+1234', 'params.teams', 'gameSprite', 'Ctrl+Enter'):
            self.assertNotIn(token, protected)
        self.assertEqual(PROGRAM.restore_text('БГ ' + protected, replacements), 'БГ ' + original)
        marker = next(iter(replacements))
        for damaged in (protected.replace(marker, ''), protected + marker):
            with self.assertRaisesRegex(ValueError, 'damaged protected marker'):
                PROGRAM.restore_text(damaged, replacements)

    def test_machine_resume_preserves_edits_and_uses_cache(self):
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=self.response) as network:
            self.run_machine()
            paths = PROGRAM.catalog_paths(self.output, 'bg')
            paths['native'].write_text('[hello]\nРъчно\n[value %0]\nБГ Value %0\n')
            self.assertEqual(self.run_machine()['filled'], 0)
            self.assertEqual(network.call_count, 1)
            # Reset all blanks to exactly the original request partition: cache
            # reuse must work without authentication or any additional spending.
            paths['native'].write_text('[hello]\n\n[value %0]\n\n')
            PROGRAM.write_json(paths['online'], {'Hello': '', '{count} items': {'one': '', 'other': ''}})
            self.assertEqual(self.run_machine()['paid_requests'], 0)
            self.assertEqual(network.call_count, 1)
        self.assertEqual(self.auth_mock.call_count, 1)
        with patch.object(PROGRAM.urllib.request, 'urlopen') as network:
            paths['native'].write_text('[hello]\nРъчно\n[value %0]\nБГ Value %0\n')
            self.run_machine()
            self.assertIn('Ръчно', paths['native'].read_text())
            network.assert_not_called()

    def test_machine_cap_reserves_before_io_and_retries_failures(self):
        import urllib.error
        def fail(request, timeout):
            ledger = json.loads((self.output / 'machine/spend-ledger.json').read_text())
            self.assertEqual(ledger['reservations'][-1]['state'], 'reserved')
            raise urllib.error.URLError('ambiguous transport failure')
        with patch.object(PROGRAM.time, 'sleep'), patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=fail) as network:
            with self.assertRaisesRegex(ValueError, 'reservation retained'):
                self.run_machine(retries=1)
            self.assertEqual(network.call_count, 2)
            ledger = json.loads((self.output / 'machine/spend-ledger.json').read_text())
            self.assertEqual(len(ledger['reservations']), 2)
            self.assertTrue(all(r['state'] == 'failed-or-ambiguous' for r in ledger['reservations']))
            spent = sum(PROGRAM.Decimal(r['reserved_usd']) for r in ledger['reservations'])
            with self.assertRaisesRegex(ValueError, 'Cost cap would be exceeded'):
                self.run_machine(max_cost_usd=str(spent))
            self.assertEqual(network.call_count, 2)

    def test_machine_http_error_body_is_preserved_without_retry(self):
        from io import BytesIO
        import urllib.error
        error = urllib.error.HTTPError('https://translation.googleapis.com/', 403, 'forbidden', {}, BytesIO(b'{"error":"denied"}'))
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=error) as network:
            with self.assertRaisesRegex(ValueError, 'reservation retained'):
                self.run_machine()
            self.assertEqual(network.call_count, 1)
        responses = list((self.output / 'machine/bg/raw').glob('*.response.bin'))
        self.assertEqual([p.read_bytes() for p in responses], [b'{"error":"denied"}'])
        ledger = json.loads((self.output / 'machine/spend-ledger.json').read_text())
        self.assertEqual(ledger['reservations'][0]['state'], 'failed-or-ambiguous')

    def test_machine_budget_prevents_first_request(self):
        with patch.object(PROGRAM.urllib.request, 'urlopen') as network:
            with self.assertRaisesRegex(ValueError, 'Cost cap would be exceeded'):
                self.run_machine(max_cost_usd='0.0000001')
            network.assert_not_called()

    def test_machine_malformed_responses_are_saved_but_never_cached(self):
        from io import BytesIO
        for payload in (b'not json', b'{}', b'{"translations":[]}',
                        b'{"translations":[{"translatedText":null}]}'):
            with patch.object(PROGRAM.urllib.request, 'urlopen', return_value=BytesIO(payload)):
                with self.assertRaises(ValueError):
                    self.run_machine()
        self.assertEqual(len(list((self.output / 'machine/bg/raw').glob('*.response.bin'))), 4)
        self.assertEqual(list((self.output / 'machine/bg/cache').glob('*')), [])
        self.assertEqual(len(json.loads((self.output / 'machine/spend-ledger.json').read_text())['reservations']), 4)

    def test_machine_rejects_marker_damage_and_oversize_before_network(self):
        from io import BytesIO
        with patch.object(PROGRAM.urllib.request, 'urlopen', return_value=BytesIO(json.dumps({
                'translations': [{'translatedText': 'БГ'}] * 5}).encode())):
            with self.assertRaisesRegex(ValueError, 'damaged protected marker'):
                self.run_machine()
        output = self.output / 'oversize'
        (self.root / PROGRAM.SOURCE_PATHS[1]).write_text('[hello]\n' + 'x' * 5001 + '\n[value %0]\nValue %0\n')
        PROGRAM.prepare(output, root=self.root, locales=('bg',))
        with patch.object(PROGRAM.urllib.request, 'urlopen') as network:
            with self.assertRaisesRegex(ValueError, 'exceeds 5000'):
                PROGRAM.machine_draft(output, 'bg', 'test-project')
            network.assert_not_called()

    def test_machine_limit_and_source_contract_integrity(self):
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=self.response):
            self.assertEqual(self.run_machine(limit_entries=1)['filled'], 1)
            self.assertEqual(self.run_machine(limit_entries=1)['filled'], 1)
        (self.output / 'source/invariants.json').write_text('{} ')
        with patch.object(PROGRAM.urllib.request, 'urlopen') as network:
            with self.assertRaisesRegex(ValueError, 'Frozen source contract changed'):
                self.run_machine()
            network.assert_not_called()

    def test_machine_only_keys_filters_exact_ids_and_checks_unknown_ids(self):
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=self.response) as network:
            self.assertEqual(self.run_machine(only_keys=['online:{count} items'])['filled'], 2)
            self.assertEqual(len(self.calls[0]['contents']), 2)
            self.assertEqual(network.call_count, 1)
            with self.assertRaisesRegex(ValueError, 'Unknown only-keys'):
                self.run_machine(only_keys=['native:missing'])
            self.assertEqual(network.call_count, 1)

    def test_machine_preserves_edit_made_during_provider_request(self):
        def edited_response(request, timeout):
            paths = PROGRAM.catalog_paths(self.output, 'bg')
            paths['native'].write_text('[hello]\nРъчна редакция\n[value %0]\n\n')
            return self.response(request, timeout)
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=edited_response):
            self.run_machine()
        self.assertIn('Ръчна редакция', PROGRAM.catalog_paths(self.output, 'bg')['native'].read_text())
        provider = json.loads((self.output / 'machine/bg/provider-drafts.json').read_text())
        self.assertTrue(any(item['translation'] == 'БГ Hello' for item in provider.values()))

    def test_windows_keeps_nonmachine_commands_usable(self):
        with patch.object(PROGRAM, 'fcntl', None):
            with self.assertRaisesRegex(ValueError, 'POSIX file locking'):
                self.run_machine()
            self.assertEqual(PROGRAM.prepare(self.output, 1, self.root), self.manifest)

    def test_machine_auth_failure_and_lock_prevent_requests(self):
        import subprocess
        with patch.object(PROGRAM.urllib.request, 'urlopen') as network:
            self.auth_mock.side_effect = subprocess.CalledProcessError(1, 'gcloud')
            with self.assertRaises(subprocess.CalledProcessError):
                self.run_machine()
            with PROGRAM.machine_lock(self.output):
                with self.assertRaisesRegex(ValueError, 'holds this kit lock'):
                    self.run_machine()
            network.assert_not_called()

    def legacy_response(self, target):
        def response(request, timeout):
            from io import BytesIO
            payload = json.loads(request.data)
            self.assertEqual(timeout, 90)
            self.assertEqual(payload['sourceLanguageCode'], 'en')
            self.assertEqual(payload['targetLanguageCode'], target)
            self.assertEqual(payload['model'], 'projects/test-project/locations/global/models/general/nmt')
            self.assertEqual(request.get_header('Authorization'), 'Bearer fake-access-token')
            return BytesIO(json.dumps({'translations': [{'translatedText': 'DRAFT ' + value}
                                                        for value in payload['contents']]}).encode())
        return response

    def test_legacy_scope_is_opt_in_and_default_scope_is_unchanged(self):
        self.assertEqual(PROGRAM.LOCALES, (
            'hi', 'bn', 'ur', 'mr', 'te', 'sw', 'ha', 'ta', 'fil', 'th', 'gu',
            'pa-Guru', 'pa-Arab', 'kn', 'am', 'yo', 'my', 'jv', 'ne', 'om',
            'pcm', 'ps', 'bho', 'fuc-Latn', 'ln', 'uz', 'ml', 'or', 'sd', 'ig', 'su', 'bg', 'nb'))
        self.assertEqual(self.manifest['locales'], list(PROGRAM.LOCALES))
        selected = PROGRAM.prepare(self.output / 'legacy', 1, self.root, locales=('cs', 'pt-PT', 'sr-Cyrl'))
        self.assertEqual(selected['locales'], ['cs', 'pt-PT', 'sr-Cyrl'])
        self.assertEqual(set(PROGRAM.NMT_TARGETS), set(PROGRAM.LEGACY_NATIVE_LOCALES) | {'bg'})
        # Game filenames are aliases, not canonical Intl/manifest language choices.
        for invalid in ('br', 'cz', 'dk', 'gr', 'si', 'sr', 'pt', 'zh-cn', 'zh-tw'):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, 'supported draft locales'):
                PROGRAM.prepare(self.output / ('invalid-' + invalid), 1, self.root, locales=(invalid,))

    def test_legacy_native_aliases_use_reviewed_explicit_nmt_targets(self):
        cases = {'cs': ('cz', 'cs'), 'da': ('dk', 'da'), 'el': ('gr', 'el'),
                 'sl': ('si', 'sl'), 'sr-Cyrl': ('sr', 'sr'),
                 'zh-Hans': ('zh-cn', 'zh-CN'), 'zh-Hant': ('zh-tw', 'zh-TW'),
                 'pt-PT': ('pt', 'pt-PT'), 'pt-BR': ('br', 'pt-BR')}
        output = self.output / 'aliases'
        PROGRAM.prepare(output, 1, self.root, locales=tuple(cases))
        for locale, (native, target) in cases.items():
            with self.subTest(locale=locale), patch.object(PROGRAM.urllib.request, 'urlopen',
                                                         side_effect=self.legacy_response(target)) as request:
                result = PROGRAM.machine_draft(output, locale, 'test-project', only_keys=['native:[hello]'])
            self.assertEqual(result['filled'], 1)
            self.assertEqual(result['paid_requests'], 1)
            request.assert_called_once()
            paths = PROGRAM.catalog_paths(output, locale)
            self.assertEqual(paths['native'].name, 'texts.' + native + '.txt')
            self.assertEqual(paths['online'].name, locale + '.json')
            self.assertIn('DRAFT Hello', paths['native'].read_text())
            saved = next((output / 'machine' / locale / 'raw').glob('*.request.json'))
            payload = json.loads(saved.read_text())['payload']
            self.assertEqual(payload['targetLanguageCode'], target)
            self.assertEqual(payload['contents'], ['Hello'])
        self.assertEqual(PROGRAM.game_code('fuc-Latn'), 'fuc')
        self.assertEqual(PROGRAM.game_code('bg'), 'bg')

    def test_native_br_metadata_is_brazilian_portuguese_not_breton(self):
        # Native game codes are historical identifiers, not necessarily ISO codes.
        metadata, errors = PROGRAM.CHECKER.read_table(PROGRAM.ROOT / 'data/texts.br.txt')
        self.assertEqual(errors, [])
        self.assertEqual(metadata['[language]'], 'Português do Brasil')
        self.assertEqual(metadata['[language-code]'], 'br')
        self.assertEqual(PROGRAM.game_code('pt-BR'), metadata['[language-code]'])
        self.assertEqual(PROGRAM.NMT_TARGETS['pt-BR'], 'pt-BR')
        self.assertIn('pt-BR', PROGRAM.LEGACY_NATIVE_LOCALES)
        self.assertNotIn('br', PROGRAM.LEGACY_NATIVE_LOCALES)
        self.assertNotIn('br', PROGRAM.SUPPORTED_DRAFT_LOCALES)
        self.assertNotIn('br', PROGRAM.NMT_TARGETS)

    def test_unreviewed_script_targets_fail_before_auth_or_network(self):
        self.auth_mock.reset_mock()
        with patch.object(PROGRAM.urllib.request, 'urlopen') as request:
            for locale in ('br', 'pa-Arab', 'pa-Guru', 'sr-Latn', 'pt', 'zh', 'fuc-Latn'):
                with self.subTest(locale=locale), self.assertRaisesRegex(ValueError, 'No reviewed Google NMT target'):
                    PROGRAM.machine_draft(self.output, locale, 'test-project')
            request.assert_not_called()
        self.auth_mock.assert_not_called()

    def test_cross_locale_ledger_cap_cache_identity_and_resume_are_shared(self):
        output = self.output / 'shared-budget'
        PROGRAM.prepare(output, 1, self.root, locales=('bg', 'cs'))
        key = ['native:[hello]']
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=self.legacy_response('bg')):
            first = PROGRAM.machine_draft(output, 'bg', 'test-project', only_keys=key)
        ledger_path = output / 'machine/spend-ledger.json'
        ledger = json.loads(ledger_path.read_text())
        spent = first['reserved_usd']
        with patch.object(PROGRAM.urllib.request, 'urlopen') as request:
            with self.assertRaisesRegex(ValueError, 'Cost cap would be exceeded'):
                PROGRAM.machine_draft(output, 'cs', 'test-project', max_cost_usd=spent, only_keys=key)
            request.assert_not_called()
        self.assertEqual(len(json.loads(ledger_path.read_text())['reservations']), 1)
        with patch.object(PROGRAM.urllib.request, 'urlopen', side_effect=self.legacy_response('cs')):
            PROGRAM.machine_draft(output, 'cs', 'test-project', only_keys=key)
        ledger = json.loads(ledger_path.read_text())
        self.assertEqual([r['locale'] for r in ledger['reservations']], ['bg', 'cs'])
        self.assertNotEqual(ledger['reservations'][0]['request_sha256'], ledger['reservations'][1]['request_sha256'])
        total = sum(PROGRAM.Decimal(r['reserved_usd']) for r in ledger['reservations'])
        # Restore a blank for a cache replay: target identity remains distinct, with no reservation.
        native = PROGRAM.catalog_paths(output, 'bg')['native']
        native.write_text(native.read_text().replace('DRAFT Hello', ''))
        with patch.object(PROGRAM.urllib.request, 'urlopen') as request:
            replay = PROGRAM.machine_draft(output, 'bg', 'test-project', only_keys=key)
            self.assertEqual(replay['paid_requests'], 0)
            self.assertEqual(replay['reserved_usd'], str(total))
            request.assert_not_called()
        self.assertEqual(len(json.loads(ledger_path.read_text())['reservations']), 2)
        self.assertIn('DRAFT Hello', native.read_text())

    def test_bg_requires_adversarial_review_without_changing_existing_gates(self):
        record, paths = self.complete_record('bg')
        issues = PROGRAM.audit_review(self.manifest, record, paths)
        self.assertTrue(any('independent' in issue for issue in issues))
        for batch in record['batches']:
            batch['adversarial_reviewer'] = 'agent-d'
            batch['adversarial_reviewed_keys'] = batch['reviewed_keys']
        self.assertEqual(PROGRAM.audit_review(self.manifest, record, paths), [])


if __name__ == '__main__':
    unittest.main()
