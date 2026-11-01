import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import io
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / 'tools/localization'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('google_draft_telugu', TOOLS / 'google_draft_telugu.py')
draft = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(draft)
requires_posix_storage = unittest.skipIf(
    draft.fcntl is None, 'Live draft storage requires POSIX locking and directory fsync')


class GoogleDraftTest(unittest.TestCase):
    def test_protection_round_trip_with_markup_and_repeated_parameters(self):
        original = 'Use {count} < UTF-8 & %0 {count}'
        protected, replacements = draft.protect(original, ['UTF-8'])
        self.assertNotIn('{count}', protected)
        self.assertEqual(draft.restore(protected, replacements), original)
        self.assertEqual(draft.restore(protected.replace('Use ', 'వాడండి '), replacements),
                         original.replace('Use ', 'వాడండి '))

    def test_native_urls_paths_and_escaped_paragraphs_are_protected(self):
        original = r'Open https://example.com/a?x=1 and data/maps/foo.map.\n\nNext %0'
        protected, replacements = draft.protect(original)
        self.assertNotIn('https://', protected)
        self.assertNotIn('data/maps', protected)
        self.assertNotIn(r'\n', protected)
        self.assertEqual(draft.restore(protected, replacements), original)

    def test_slash_labels_are_translatable_but_routes_remain_atomic(self):
        protected, replacements = draft.protect('Other/unknown Loads/min Food/min /ais/mine /play/')
        self.assertIn('Other/unknown Loads/min Food/min', protected)
        self.assertNotIn('/ais/mine', protected)
        self.assertNotIn('/play/', protected)
        self.assertEqual(draft.restore(protected, replacements),
                         'Other/unknown Loads/min Food/min /ais/mine /play/')

    def test_product_ai_and_programming_names_are_not_translated(self):
        original = 'Castor plays Globulation 2 using JavaScript; compare Maxima.'
        protected, replacements = draft.protect(original)
        for name in ['Castor', 'Globulation 2', 'JavaScript', 'Maxima']:
            self.assertNotIn(name, protected)
        self.assertEqual(draft.restore(protected, replacements), original)
        protected, replacements = draft.protect('Castor and CastorOil')
        self.assertIn('CastorOil', protected)
        self.assertEqual(replacements, [('GLOB2TOKEN0000', 'Castor')])
        self.assertEqual(draft.restore(protected, replacements), 'Castor and CastorOil')

    def test_malformed_and_missing_tokens_rejected(self):
        protected, replacements = draft.protect('Use %0')
        for broken in [protected.replace('GLOB2TOKEN0000', ''), protected + protected,
                       protected + '<script>x</script>', protected.replace('</span>', ''),
                       protected + 'GLOB2TOKEN9999']:
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                draft.restore(broken, replacements)

    @requires_posix_storage
    def test_failed_attempt_remains_charged_and_cached_resume_is_free(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            with self.assertRaises(TimeoutError):
                draft.translate(folder, 'bradley', ['abc'], lambda _: (_ for _ in ()).throw(TimeoutError()))
            ledger = json.loads((folder / 'ledger.json').read_text())
            self.assertEqual(len(ledger['attempts']), 1)
            result = draft.translate(folder, 'bradley', ['abc'],
                                     lambda _: {'translations': [{'translatedText': 'తెలుగు'}]})
            self.assertEqual(result, ['తెలుగు'])
            paths = list((folder / 'responses').glob('*.json'))
            original_bytes = paths[0].read_bytes()
            result = draft.translate(folder, 'bradley', ['abc'], lambda _: self.fail('Cache must skip network'))
            self.assertEqual(result, ['తెలుగు'])
            self.assertEqual(paths[0].read_bytes(), original_bytes)
            self.assertEqual(len(json.loads((folder / 'ledger.json').read_text())['attempts']), 2)

    @requires_posix_storage
    def test_cap_reservation_precedes_network(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            draft.atomic_json(folder / 'ledger.json', {'project': 'bradley', 'attempts': [
                {'reserved_microdollars': draft.CAP_MICRODOLLARS - 10}]})
            with self.assertRaisesRegex(ValueError, 'cap'):
                draft.translate(folder, 'bradley', ['a'], lambda _: self.fail('Budget violation contacted network'))
            with self.assertRaisesRegex(ValueError, 'another project'):
                draft.reserve(folder, 'other', {'contents': ['a']})

    @requires_posix_storage
    def test_malformed_response_is_retained_and_not_rebilled(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            with self.assertRaisesRegex(ValueError, 'Malformed'):
                draft.translate(folder, 'bradley', ['a'], lambda _: {'translations': []})
            with self.assertRaisesRegex(ValueError, 'Malformed'):
                draft.translate(folder, 'bradley', ['a'], lambda _: self.fail('Malformed cache must not retry'))
            self.assertEqual(len(json.loads((folder / 'ledger.json').read_text())['attempts']), 1)

    @unittest.skipIf(draft.fcntl is None, 'Live run locking requires fcntl')
    @requires_posix_storage
    def test_stale_source_fails_before_authentication(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            draft.atomic_json(folder / 'manifest.json', {'locales': ['te'], 'source_sha256': 'old'})
            with patch.object(draft.program, 'sources', return_value=(None, None, None, None, 'new')):
                with self.assertRaisesRegex(ValueError, 'stale'):
                    draft.run(folder, 'bradley', execute=True)

    @unittest.skipIf(draft.fcntl is None, 'Live run locking requires fcntl')
    @requires_posix_storage
    def test_pilot_resume_reuses_outputs_and_preserves_manual_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            native = {'[label %d]' % i: 'Source %d' % i for i in range(10)}
            native.update({'[language-code]': 'en', '[language]': 'English',
                           '[language incomplete]': 'English - Incomplete'})
            draft.atomic_json(folder / 'manifest.json', {'locales': ['te'], 'source_sha256': 'current'})
            draft.atomic_json(folder / 'source/native.json', native)
            draft.atomic_json(folder / 'source/online.json', {'Online': 'Online'})
            (folder / 'source/native-keys.txt').write_text('\n'.join(native))
            paths = draft.program.catalog_paths(folder, 'te')
            paths['native'].parent.mkdir(parents=True)
            paths['native'].write_text(''.join(key + '\n\n' for key in native))
            draft.atomic_json(paths['online'], {'Online': ''})
            sent = []
            def sender(payload):
                sent.extend(payload['contents'])
                return {'translations': [{'translatedText': 'తెలుగు ' + value} for value in payload['contents']]}
            with patch.object(draft.program, 'sources', return_value=(None, None, None, None, 'current')), \
                 patch.object(draft.subprocess, 'check_output', return_value='{"projectId":"bradley","name":"Wrong project"}'), \
                 patch.object(draft, 'request_sender', side_effect=AssertionError('Wrong billing project contacted')):
                with self.assertRaisesRegex(ValueError, 'Project ID/display name'):
                    draft.run(folder, 'bradley', execute=True)
                self.assertFalse((folder / 'google/ledger.json').exists())
            with patch.object(draft.program, 'sources', return_value=(None, None, None, None, 'current')), \
                 patch.object(draft.subprocess, 'check_output', return_value='{"projectId":"bradley","name":"Bradley personal"}'), \
                 patch.object(draft, 'request_sender', return_value=sender):
                draft.run(folder, 'bradley', execute=True, pilot=True)
                pilot_sent = list(sent)
                values, _ = draft.program.CHECKER.read_table(paths['native'])
                for key, value in draft.TELUGU_METADATA.items():
                    self.assertEqual(values[key], value)
                    self.assertNotIn(native[key], sent)
                filled = [key for key, value in values.items() if value]
                values[filled[0]] = 'మానవ సంపాదన'
                values[filled[1]] = ''  # Simulate resuming a lost draft value from raw evidence.
                paths['native'].write_text(''.join(key + '\n' + values[key] + '\n' for key in native))
                sent.clear()
                draft.run(folder, 'bradley', execute=True)
                self.assertFalse(set(sent) & set(pilot_sent))
                final, _ = draft.program.CHECKER.read_table(paths['native'])
                self.assertEqual(final[filled[0]], 'మానవ సంపాదన')
                self.assertTrue(all(final.values()))

    def test_access_token_cached_and_refreshed_without_printing(self):
        sender = draft.request_sender('bradley')
        with patch.object(draft.subprocess, 'check_output', return_value='private-token\n') as auth, \
             patch.object(draft.time, 'monotonic', side_effect=[1, 2, 3000, 3000]), \
             patch.object(draft.urllib.request, 'urlopen', side_effect=lambda *args, **kwargs: io.BytesIO(b'{}')), \
             patch('sys.stdout', new_callable=io.StringIO) as output:
            sender({})
            sender({})
            sender({})
            self.assertEqual(auth.call_count, 2)
            self.assertNotIn('private-token', output.getvalue())


if __name__ == '__main__':
    unittest.main()
