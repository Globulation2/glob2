"""Thai catalog boundary suggestions preserve text and avoid known ICU misbreaks."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('thai_breaks', Path(__file__).resolve().parents[1] / 'tools/localization/thai_breaks.py')
BREAKS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BREAKS)


@unittest.skipUnless(importlib.util.find_spec('pythainlp'), 'Install requirements-dev.txt for Thai segmentation tests')
class ThaiBreakTests(unittest.TestCase):
    def test_thai_words_and_marks_stay_whole(self):
        for text, expected in [('อาคารอื่นๆ', 'อาคาร\u200bอื่นๆ'), ('เป็นกลุ่ม', 'เป็น\u200bกลุ่ม'),
                               ('อัปโหลด', 'อัปโหลด'), ('ริมฝั่งฟยอร์ด', 'ริมฝั่ง\u200bฟยอร์ด')]:
            with self.subTest(text=text):
                self.assertEqual(BREAKS.annotate(text), expected)
        text = 'ผู้เล่นกำลังก่อสร้าง %0 / 100 Glob2'
        result = BREAKS.annotate(text)
        self.assertEqual(result.replace('\u200b', ''), text)
        self.assertNotRegex(result, '\u200b[\u0e31\u0e34-\u0e3a\u0e47-\u0e4e]')
        self.assertEqual(BREAKS.annotate(result), result)

    def test_catalog_preserves_contracts(self):
        text = '[language]\nภาษาไทย\n[blank]\n\n[profile]\nหัวข้อ: ภาษาไทย\\n\\nCPU: %0 {count} https://example.com\n'
        self.assertEqual(BREAKS.catalog(text).replace('\u200b', ''), text)
        with self.assertRaises(ValueError):
            BREAKS.catalog('[missing value]\n')


if __name__ == '__main__':
    unittest.main()
