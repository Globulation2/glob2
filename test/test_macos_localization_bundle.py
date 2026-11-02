import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('macos_bundle_qa',ROOT/'tools/check_macos_localization_bundle.py')
qa=importlib.util.module_from_spec(spec);spec.loader.exec_module(qa)

class BundleResources(unittest.TestCase):
    def fixture(self,root):
        bundle=root/'build/Glob2.app';resources=bundle/'Contents/Resources'
        for name in qa.ASSETS:
            for base in (root/'content',resources):
                path=base/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(name.encode())
        purpose='Discover and join local games.'
        for path in (root/'platforms/macos/Info.plist',bundle/'Contents/Info.plist'):
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(plistlib.dumps({'NSLocalNetworkUsageDescription':purpose}))
        for path in (root/'platforms/macos/th.lproj/InfoPlist.strings',resources/'th.lproj/InfoPlist.strings'):
            path.parent.mkdir(parents=True,exist_ok=True);path.write_text('"NSLocalNetworkUsageDescription" = "ค้นหาและเข้าร่วมเกม";',encoding='utf-8')
        binary=bundle/'Contents/MacOS/glob2';binary.parent.mkdir();binary.write_bytes(b'packaged binary')
        return bundle
    def test_all_assets_and_actual_thai_permission_are_bound_to_source(self):
        with tempfile.TemporaryDirectory()as temp:
            root=Path(temp);bundle=self.fixture(root);report=qa.bundle_resources(root,bundle)
            self.assertEqual(len(report['resource_sha256']),len(qa.ASSETS)+1)
            self.assertEqual(report['thai_purpose'],'ค้นหาและเข้าร่วมเกม')
            (bundle/'Contents/Resources/data/fonts/scripts/NotoSansThai-Regular.ttf').write_bytes(b'wrongfont')
            with self.assertRaisesRegex(ValueError,'bundled asset'):qa.bundle_resources(root,bundle)
    def test_misplaced_localization_and_wrong_english_purpose_cannot_pass(self):
        with tempfile.TemporaryDirectory()as temp:
            root=Path(temp);bundle=self.fixture(root);path=bundle/'Contents/Resources/th.lproj/InfoPlist.strings'
            data=path.read_bytes();path.unlink()
            with self.assertRaisesRegex(ValueError,'misplaced'):qa.bundle_resources(root,bundle)
            path.write_bytes(data);(bundle/'Contents/Info.plist').write_bytes(plistlib.dumps({'NSLocalNetworkUsageDescription':'Wrong scope'}))
            with self.assertRaisesRegex(ValueError,'English'):qa.bundle_resources(root,bundle)

if __name__=='__main__':unittest.main()
