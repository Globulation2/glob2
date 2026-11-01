import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('android_localization_qa',ROOT/'mobile/android_localization_qa.py')
qa=importlib.util.module_from_spec(spec);spec.loader.exec_module(qa)

class ApkLocalizationIntegrity(unittest.TestCase):
    def test_reviewed_catalog_font_and_notices_must_match_actual_apk(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);apk=root/'app.apk'
            for name in qa.FILES:
                p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(name.encode())
            with zipfile.ZipFile(apk,'w') as z:
                for name in qa.FILES:z.write(root/name,'assets/glob2-bundle/'+name)
            self.assertEqual(set(qa.apk_hashes(root,apk)),set(qa.FILES)|{'apk'})
            (root/qa.FILES[1]).write_bytes(b'new unreviewed font')
            with self.assertRaisesRegex(ValueError,'font'):qa.apk_hashes(root,apk)

    def test_missing_thai_catalog_cannot_use_english_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);apk=root/'app.apk';p=root/qa.FILES[0];p.parent.mkdir(parents=True);p.write_text('Thai')
            with zipfile.ZipFile(apk,'w') as z:z.writestr('assets/glob2-bundle/data/texts.en.txt','English')
            with self.assertRaises(KeyError):qa.apk_hashes(root,apk)

if __name__=='__main__':unittest.main()
