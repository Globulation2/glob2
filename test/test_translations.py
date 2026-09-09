#!/usr/bin/env python3
"""Regression tests for translation corruption that can break the game loader."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('check_translations', Path(__file__).resolve().parents[1] / 'data/check_translations.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class TranslationAuditTest(unittest.TestCase):
    def test_duplicate_values_are_reported_without_rewriting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'texts.test.txt'
            original = '[hp]\nHP\n[hp]\nHealth\n'
            path.write_text(original, encoding='utf-8')
            entries, errors = checker.read_table(path)
            self.assertEqual(entries['[hp]'], 'Health')
            self.assertTrue(any('duplicate key' in e for e in errors))
            self.assertEqual(path.read_text(encoding='utf-8'), original)

    def test_missing_value_is_not_an_intentional_empty_value(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'texts.test.txt'
            path.write_text('[inn explanation 2]\n', encoding='utf-8')
            self.assertTrue(checker.read_table(path)[1])
            path.write_text('[inn explanation 2]\n\n', encoding='utf-8')
            self.assertEqual(checker.read_table(path), ({'[inn explanation 2]': ''}, []))

    def test_placeholders_preserve_identity_and_multiplicity(self):
        self.assertNotEqual(checker.placeholders('%0 %1'), checker.placeholders('%0 %0'))
        self.assertEqual(checker.placeholders('%0 %1'), checker.placeholders('%1 %0'))
        self.assertEqual(checker.placeholders('%0% uploaded'), {'%0': 1})
        self.assertTrue(checker.invalid_percent('%s'))
        self.assertFalse(checker.invalid_percent('%0%'))

    def test_audit_finds_missing_key_and_bad_placeholder_in_context(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'data').mkdir()
            (root / 'src').mkdir()
            (root / 'src/test.cpp').write_text('getString("[missing lookup]");', encoding='utf-8')
            (root / 'data/texts.list.txt').write_text('data/texts.keys.txt\ndata/texts.en.txt\ndata/texts.xx.txt\n', encoding='utf-8')
            keys = ['[language-code]', '[language]', '[language incomplete]', '[language-tr]', '[%0 to %1]', '[inn explanation 2]', '[new control]', '[school explanation]', '[school explanation 2]', '[absent]']
            (root / 'data/texts.keys.txt').write_text('\n'.join(keys)+'\n', encoding='utf-8')
            english = ['en', 'English', 'English - Incomplete', 'Language', '%0 to %1', '', 'New control', 'Improves construction', 'for workers', 'Present in English']
            other = ['xx', 'Example', 'Example - Incomplete', 'Language', '%0 to %0', '', '', 'Localized complete sentence', '']
            for code, values in [('en', english), ('xx', other)]:
                (root / f'data/texts.{code}.txt').write_text(''.join(k+'\n'+v+'\n' for k,v in zip(keys, values)), encoding='utf-8')
            result = checker.audit(root)
            self.assertTrue(any('unknown translation key [missing lookup]' in e for e in result['errors']))
            self.assertTrue(any('invalid placeholders' in e for e in result['errors']))
            self.assertEqual(result['languages']['data/texts.xx.txt']['untranslated'], ['[absent]', '[new control]'])
            self.assertTrue(any('missing key [absent]' in e for e in result['errors']))
            self.assertTrue(any('would show an English second line' in e for e in result['errors']))
            (root / 'data/texts.incomplete.txt').write_text('data/texts.xx.txt\ndata/texts.en.txt\n', encoding='utf-8')
            self.assertTrue(any('must match texts.list.txt in order' in e for e in checker.audit(root)['errors']))

    def test_explicit_settings_fallbacks_do_not_hide_other_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'data').mkdir()
            metadata = ['[language-code]', '[language]', '[language incomplete]', '[language-tr]']
            keys = metadata + ['[settings New control]', '[existing control]']
            (root / 'data/texts.list.txt').write_text('data/texts.keys.txt\ndata/texts.en.txt\ndata/texts.xx.txt\n')
            (root / 'data/texts.keys.txt').write_text('\n'.join(keys)+'\n')
            english = dict(zip(keys, ['en', 'English', 'English incomplete', 'Language', 'New control', 'Existing']))
            other = dict(zip(metadata, ['xx', 'Example', 'Example incomplete', 'Language']))
            other['[existing control]'] = 'Localized'
            def write(code, entries):
                (root / f'data/texts.{code}.txt').write_text(''.join(k+'\n'+v+'\n' for k,v in entries.items()))
            write('en', english)
            write('xx', other)
            allowlist = root / 'data/settings-english-fallbacks.txt'
            allowlist.write_text('[settings New control]\n')
            report = checker.audit(root)
            self.assertEqual(report['errors'], [])
            self.assertEqual(report['languages']['data/texts.xx.txt']['english_fallbacks'], ['[settings New control]'])
            # The untranslated count still honestly reports work for translators.
            self.assertEqual(report['languages']['data/texts.xx.txt']['untranslated'], ['[settings New control]'])
            del other['[existing control]']
            write('xx', other)
            self.assertTrue(any('missing key [existing control]' in e for e in checker.audit(root)['errors']))
            allowlist.write_text('[existing control]\n')
            self.assertTrue(any('invalid fallback key' in e for e in checker.audit(root)['errors']))
            allowlist.write_text('[settings New control]\n')
            english['[settings New control]'] = ''
            write('en', english)
            self.assertTrue(any('missing English fallback' in e for e in checker.audit(root)['errors']))


if __name__ == '__main__':
    unittest.main()
