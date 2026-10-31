#!/usr/bin/env python3
"""Regression tests for translation corruption that can break the game loader."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('check_translations', Path(__file__).resolve().parents[1] / 'data/check_translations.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class TranslationAuditTest(unittest.TestCase):
    def test_catalogs_do_not_reintroduce_english_fallbacks(self):
        root = Path(__file__).resolve().parents[1]
        allowed = json.loads((root / 'test/translation_shared_values.json').read_text())
        english, errors = checker.read_table(root / 'data/texts.en.txt')
        self.assertEqual(errors, [])
        for name in checker.read_lines(root / 'data/texts.list.txt')[1:]:
            if name == 'data/texts.en.txt':
                continue
            translated, errors = checker.read_table(root / name)
            self.assertEqual(errors, [])
            vocabulary = set(allowed['standard_names_and_key_legends']) | set(
                allowed['shared_vocabulary'][Path(name).name])
            for key in checker.read_lines(root / 'data/texts.keys.txt'):
                source = english[key]
                with self.subTest(catalog=name, key=key):
                    value = translated.get(key)
                    self.assertIsNotNone(value)
                    if source and source not in vocabulary:
                        self.assertNotEqual(value, source, 'English fallback')
                    if key.endswith('-Profile]'):
                        self.assertEqual(value.count(r'\n\n'), source.count(r'\n\n'))
                        self.assertTrue(all(part.strip() for part in value.split(r'\n\n')))
                        for section in value.split(r'\n\n'):
                            heading, separator, body = section.partition(':')
                            self.assertTrue(separator and heading.strip() and body.strip(),
                                            'Profile sections need a heading and body separated by ASCII colon')

    def test_source_scanner_covers_wrappers_arrays_and_multiline_literals(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Production.cpp'
            path.write_text(r'''// getString("[comment]")
/* const char *unused = "[block comment]"; */
const char *example = "the example \"[quoted example]\" is prose";
const char *character = '\"';
getString(
    "[multi" /* spacing */ "line]");
fe::tr(selected ? "[first branch]" : "[second branch]");
const char *labels[] = {"[array key]", R"key([raw key])key"};
musicText(ai.value("metadata field", false) ? "Like" : "Liked");
customAIText("AI label");
libraryText("Library label");
''', encoding='utf-8')
            found = dict(checker.source_keys(path))
            self.assertEqual(set(found), {'[multiline]', '[first branch]',
                '[second branch]', '[array key]', '[raw key]', '[music Like]',
                '[music Liked]', '[AI label]', '[library Library label]'})
            self.assertEqual(found['[multiline]'], 6)

    def test_settings_scanner_covers_implicit_helpers_and_dynamic_categories(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'settings/SettingsScreen.cpp'
            path.parent.mkdir()
            path.write_text('''section("Gesture scrolling");
toggle("control.id", "Wheel enabled",
    "Wheel help", enabled, [](int) {});
choice("mode", selected ? "First label" : "Second label", "", 0, {}, {});
ScrollSlider{"slider.id", "Momentum", "Momentum help", &Settings::momentum};
const char *names[] = {"Controls", "Audio"};
return tr(names[int(category)]);
info(tr("Implicit label"));
return Glob2UI::tr("[" + text + "]");
''', encoding='utf-8')
            self.assertEqual({key for key, _ in checker.source_keys(path)}, {
                '[settings Gesture scrolling]', '[settings Wheel enabled]',
                '[settings Wheel help]', '[settings First label]',
                '[settings Second label]', '[settings Momentum]',
                '[settings Momentum help]', '[settings Controls]',
                '[settings Audio]', '[settings Implicit label]'})

    def test_direct_frontend_prose_is_checked_without_flagging_symbols_or_authored_names(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'screens/Example.cpp'
            path.parent.mkdir()
            path.write_text(r'''button("id", "Untranslated control", [] {});
paragraph(selected ? "First branch" : "Second branch");
caption("\xE2\x99\xA5 " + count);
button("format", ".opus", [] {});
label(player.name);
paragraph(tr("[localized]"));
choice("mode", {"Untranslated option", tr("[localized]")}, 0, {});
panel.actions = {{"id", "Untranslated action", [] {}}, {"ok", tr("[localized]"), [] {}}};
// paragraph("Comment only");
''', encoding='utf-8')
            self.assertEqual({value for value, _ in checker.source_ui_literals(path)},
                {'Untranslated control', 'First branch', 'Second branch', 'Untranslated option', 'Untranslated action'})

    def test_computed_catalog_keys_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metric = root / 'MetricCatalog.cpp'
            metric.write_text('add({.id = "ground movement", .group = Group::Population});')
            self.assertEqual({key for key, _ in checker.source_keys(metric)}, {
                '[stat ground movement]', '[stat ground movement about]'})
            experiment = root / 'ExperimentalFeatures.cpp'
            experiment.write_text('{ExperimentId::New, "new-experiment", "New", "Help"},')
            self.assertEqual({key for key, _ in checker.source_keys(experiment)}, {
                '[experiment new-experiment]', '[experiment new-experiment help]'})

    def test_frozen_tutorial_save_aliases_cover_archived_sources(self):
        root = Path(__file__).resolve().parents[1]
        spec = importlib.util.spec_from_file_location('tutorial_aliases', root / 'test/support/legacy_tutorial_aliases.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        self.assertEqual((root / generator.OUTPUT).read_text(encoding='utf-8'), generator.render(root))
        english, errors = checker.read_table(root / 'data/texts.en.txt')
        self.assertFalse(errors)
        aliases = generator.aliases(root)
        for _, source in generator.sources(root):
            self.assertEqual(generator.fingerprint(source), generator.fingerprint(source.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')))
        self.assertGreater(len(aliases), 900)  # Includes older map-embedded copies.
        for key in aliases.values():
            self.assertTrue(english.get(key), key)
        # One legacy Spanish instruction was used for both steps; its current
        # combined wording must remain valid for either display alias.
        self.assertEqual(english['[tutorial part2 message 29]'], english['[tutorial part2 message 30]'])
        for name in checker.read_lines(root / 'data/texts.list.txt')[1:]:
            table, errors = checker.read_table(root / name)
            self.assertFalse(errors)
            with self.subTest(catalog=name):
                self.assertTrue(table.get('[tutorial part2 message 29]'))
                self.assertEqual(table.get('[tutorial part2 message 29]'),
                                 table.get('[tutorial part2 message 30]'),
                                 'Ambiguous legacy pool aliases need the same complete instruction')
        for part, count in ((1, 28), (2, 39), (3, 30), (4, 19)):
            script = (root / f'test/fixtures/tutorial/legacy/tutorial_part{part}.sgsl').read_text(encoding='utf-8')
            groups = {key for key, _ in generator.messages(script, part)}
            self.assertEqual(len(groups), count)
            self.assertTrue(groups <= set(aliases.values()))

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
            (root / 'scripts').mkdir()
            (root / 'scripts/tutorial_part1.sgsl').write_text(
                '#show("Ignored")\nshow("Message with \"inner quotes\".")\nshow("Message",xx)\n', encoding='utf-8')
            (root / 'data/themes').mkdir()
            (root / 'data/themes/index.json').write_text('{"themes":["missing-theme"]}', encoding='utf-8')
            result = checker.audit(root)
            self.assertEqual(result['coverage']['tutorials']['scripts/tutorial_part1.sgsl'], {
                'message_counts': {'en': 1, 'xx': 1}, 'legacy_languages_without_messages': [],
                'display_catalog_gaps': {'data/texts.en.txt': ['[tutorial part1 message 1]'],
                    'data/texts.xx.txt': ['[tutorial part1 message 1]']}})
            self.assertTrue(any('unknown translation key [theme missing-theme]' in e for e in result['errors']))
            self.assertTrue(any('unknown translation key [missing lookup]' in e for e in result['errors']))
            self.assertTrue(any('invalid placeholders' in e for e in result['errors']))
            self.assertEqual(result['languages']['data/texts.xx.txt']['untranslated'], ['[absent]', '[new control]'])
            self.assertTrue(any('missing key [absent]' in e for e in result['errors']))
            self.assertTrue(any('would show an English second line' in e for e in result['errors']))
            # An approved fallback only affects completeness, never structural errors.
            (root / 'data/texts.pending.txt').write_text('[new control]\n[unknown]\n', encoding='utf-8')
            pending_result = checker.audit(root)
            self.assertEqual(pending_result['languages']['data/texts.xx.txt']['pending'], ['[new control]'])
            self.assertEqual(pending_result['languages']['data/texts.en.txt']['pending'], [])
            self.assertTrue(any('unknown pending key [unknown]' in e for e in pending_result['errors']))
            self.assertTrue(any('invalid placeholders' in e for e in pending_result['errors']))
            self.assertIn('[absent]', pending_result['languages']['data/texts.xx.txt']['untranslated'])
            (root / 'data/texts.incomplete.txt').write_text('data/texts.xx.txt\ndata/texts.en.txt\n', encoding='utf-8')
            self.assertTrue(any('must match texts.list.txt in order' in e for e in checker.audit(root)['errors']))


if __name__ == '__main__':
    unittest.main()
