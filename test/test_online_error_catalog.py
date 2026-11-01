"""The native bridge consumes reviewed catalogs, never another translation source."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('online_error_catalog', ROOT / 'tools/localization/build_online_error_catalog.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

class OnlineErrorCatalogTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.en, self.bg, self.output = [self.folder / name for name in ('en.json', 'bg.json', 'Catalog.h')]
        self.english = {MODULE.GENERIC: MODULE.GENERIC, 'Hello {slot0}': 'Hello {slot0}'}
        self.bulgarian = {MODULE.GENERIC: 'Грешка.', 'Hello {slot0}': 'Здравей, {slot0}'}
    def generate(self):
        for path, content in ((self.en, self.english), (self.bg, self.bulgarian)):
            path.write_text(json.dumps(content, ensure_ascii=False))
        return MODULE.generate(self.en, self.bg, self.output)
    def test_exact_hash_binding_determinism_and_ascii_cpp_chunks(self):
        result = self.generate()
        first = self.output.read_bytes()
        self.assertEqual(result['english_sha256'], hashlib.sha256(self.en.read_bytes()).hexdigest())
        self.assertEqual(result['bulgarian_sha256'], hashlib.sha256(self.bg.read_bytes()).hexdigest())
        self.assertTrue(first.isascii())
        self.assertIn(b'\\u0417', first)
        self.assertIn(result['bulgarian_sha256'].encode(), first)
        self.generate()
        self.assertEqual(first, self.output.read_bytes())
        self.bulgarian['Hello {slot0}'] = 'Привет, {slot0}'
        changed = self.generate()
        self.assertNotEqual(result['bulgarian_sha256'], changed['bulgarian_sha256'])
        self.assertNotEqual(first, self.output.read_bytes())
    def test_inventory_and_placeholder_mismatches_are_not_embedded(self):
        self.bulgarian.pop('Hello {slot0}')
        with self.assertRaisesRegex(ValueError, 'inventories'): self.generate()
        self.bulgarian['Hello {slot0}'] = 'Здравей'
        with self.assertRaisesRegex(ValueError, 'Placeholder'): self.generate()
    def test_invalid_target_and_missing_reviewed_generic_fail_closed(self):
        self.bulgarian['Hello {slot0}'] = 'bad\0 {slot0}'
        with self.assertRaisesRegex(ValueError, 'Invalid translation'): self.generate()
        del self.english[MODULE.GENERIC]; del self.bulgarian[MODULE.GENERIC]
        self.bulgarian['Hello {slot0}'] = 'Здравей, {slot0}'
        with self.assertRaisesRegex(ValueError, 'generic'): self.generate()
    def test_plural_objects_are_excluded_without_guessing_a_form(self):
        self.english['{count} days'] = {'one': '{count} day', 'other': '{count} days'}
        self.bulgarian['{count} days'] = {'one': '{count} ден', 'other': '{count} дни'}
        result = self.generate()
        self.assertEqual(result['excluded_plural_count'], 1)
        self.assertNotIn(b'{count} days', self.output.read_bytes())
    def test_large_catalog_chunks_do_not_exceed_msvc_literal_limits(self):
        self.english['Long {slot0}'] = 'x' * 20000 + '{slot0}'
        self.bulgarian['Long {slot0}'] = 'я' * 20000 + '{slot0}'
        self.generate()
        chunks = re.findall(r'R"(G2ERR[0-9a-f]+)\((.*?)\)\1"', self.output.read_text(), re.S)
        self.assertGreater(len(chunks), 2)
        self.assertTrue(all(len(value) <= 8192 for _, value in chunks))
        payload = json.loads(''.join(value for _, value in chunks))
        self.assertEqual(payload['messages']['Long {slot0}'], self.bulgarian['Long {slot0}'])
        sized_chunks = re.findall(r'\{R"(G2ERR[0-9a-f]+)\((.*?)\)\1", (\d+)\}', self.output.read_text(), re.S)
        self.assertEqual(len(sized_chunks), len(chunks))
        self.assertTrue(all(len(value.encode('ascii')) == int(size) for _, value, size in sized_chunks))
    def test_current_authoritative_catalog_hashes_and_size(self):
        en = ROOT / 'platform/packages/i18n/locales/en.json'
        bg = ROOT / 'platform/packages/i18n/locales/bg.json'
        result = MODULE.generate(en, bg, self.output)
        self.assertEqual(result['english_sha256'], hashlib.sha256(en.read_bytes()).hexdigest())
        self.assertEqual(result['bulgarian_sha256'], hashlib.sha256(bg.read_bytes()).hexdigest())
        self.assertLess(result['generated_bytes'], 1000000)
        self.assertTrue(self.output.read_bytes().isascii())
        english = json.loads(en.read_text())
        self.assertEqual(result['message_count'], sum(isinstance(v, str) for v in english.values()))
    def test_ambiguous_english_literals_require_structured_keys(self):
        self.english['legacy-id'] = MODULE.GENERIC
        self.bulgarian['legacy-id'] = 'Друга грешка.'
        self.generate()
        chunks = re.findall(r'R"(G2ERR[0-9a-f]+)\((.*?)\)\1"', self.output.read_text(), re.S)
        payload = json.loads(''.join(value for _, value in chunks))
        self.assertTrue(payload['ambiguousLiterals'][MODULE.GENERIC])
        self.assertNotIn(MODULE.GENERIC, payload['literals'])
        self.assertEqual(payload['messages']['legacy-id'], 'Друга грешка.')
    def test_thai_resource_is_independently_bound_and_validation_is_atomic(self):
        self.generate()
        thai = self.folder / 'th.json'
        content = {MODULE.GENERIC: 'เกิดข้อผิดพลาด', 'Hello {slot0}': 'สวัสดี {slot0}'}
        thai.write_text(json.dumps(content, ensure_ascii=False))
        result = MODULE.generate(self.en, self.bg, self.output, thai)
        self.assertEqual(result['thai_sha256'], hashlib.sha256(thai.read_bytes()).hexdigest())
        first = self.output.read_bytes()
        self.assertIn(b'namespace Online::ThaiErrorCatalogData', first)
        self.assertIn(b'ThaiSha256', first)
        self.assertIn(b'BulgarianSha256', first)
        self.assertTrue(first.isascii())
        content['Hello {slot0}'] = 'สวัสดี'
        thai.write_text(json.dumps(content, ensure_ascii=False))
        with self.assertRaisesRegex(ValueError, 'Placeholder'):
            MODULE.generate(self.en, self.bg, self.output, thai)
        self.assertEqual(self.output.read_bytes(), first)
    def test_exact_legacy_alias_is_retained_when_english_value_differs_from_key(self):
        self.english['legacy-key'] = 'An exact old server literal.'
        self.bulgarian['legacy-key'] = 'Преведен текст.'
        self.generate()
        self.assertIn('An exact old server literal.', self.output.read_text())
        chunks = re.findall(r'R"(G2ERR[0-9a-f]+)\((.*?)\)\1"', self.output.read_text(), re.S)
        payload = json.loads(''.join(value for _, value in chunks))
        self.assertTrue(payload['nonliteralKeys']['legacy-key'])
        self.assertEqual(payload['literals']['An exact old server literal.'], 'legacy-key')

if __name__ == '__main__': unittest.main()
