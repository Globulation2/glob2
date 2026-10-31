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


if __name__ == '__main__':
    # Hosted Linux verification runs this workflow entry point. Include the
    # provider's budget/resumption contracts; discovery imports its file itself.
    from test_google_draft import GoogleDraftTest
    unittest.main()
