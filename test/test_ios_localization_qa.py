"""Portable contracts for iOS-only localization qualification."""
import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
import sys
from unittest.mock import patch

# Use the tracked helper after patch integration, or the artifact proposal for review.
ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT/'mobile/ios_localization_qa.py'
spec = importlib.util.spec_from_file_location('ios_localization_qa', HELPER)
qa = importlib.util.module_from_spec(spec); spec.loader.exec_module(qa)


class SimulatorSelection(unittest.TestCase):
    def test_newest_available_ios_and_compatible_phone_only(self):
        phone = {'identifier':'phone-a','name':'iPhone A','productFamily':'iPhone'}
        incompatible = {'identifier':'phone-z','name':'iPhone Z','productFamily':'iPhone'}
        runtimes = [dict(identifier='com.apple.iOS-26',version='26.0',isAvailable=True),
                    dict(identifier='com.apple.iOS-27',version='27.0',isAvailable=True,supportedDeviceTypes=[phone]),
                    dict(identifier='com.apple.iOS-28',version='28.0',isAvailable=False),
                    dict(identifier='com.apple.tvOS-99',version='99',isAvailable=True)]
        runtime, selected = qa.select_device(runtimes,[phone,incompatible])
        self.assertEqual(runtime['version'],'27.0'); self.assertEqual(selected,phone)

    def test_missing_runtime_or_compatible_phone_cannot_pass(self):
        with self.assertRaisesRegex(ValueError,'runtime'):qa.select_device([],[])
        runtime = dict(identifier='com.apple.iOS-27',version='27',isAvailable=True,
                       supportedDeviceTypes=[dict(identifier='pad')])
        with self.assertRaisesRegex(ValueError,'compatible'):qa.select_device([runtime],[dict(identifier='phone',name='iPhone A')])


class BundleIntegrity(unittest.TestCase):
    def fixture(self, root, app):
        (root/'mobile/ios/th.lproj').mkdir(parents=True)
        (root/'mobile/ios/Info.plist.in').write_text('<key>NSLocalNetworkUsageDescription</key><string>Connect locally.</string>')
        (root/'mobile/ios/th.lproj/InfoPlist.strings').write_text('"NSLocalNetworkUsageDescription" = "เชื่อมต่อผ่านเครือข่ายในพื้นที่";')
        app.mkdir(); (app/'th.lproj').mkdir()
        (app/'Info.plist').write_bytes(plistlib.dumps({'NSLocalNetworkUsageDescription':'Connect locally.'}))
        (app/'Glob2').write_bytes(b'executable')
        (app/'th.lproj/InfoPlist.strings').write_bytes(b'compiled localized plist')
        for name in qa.FILES:
            for base in (root,app):
                p=base/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(name.encode())

    def test_exact_bundle_and_individual_asset_hashes(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);app=root/'app';self.fixture(root,app)
            hashes=qa.bundle_checks(root,app,'เชื่อมต่อผ่านเครือข่ายในพื้นที่')
            self.assertIn('Glob2',hashes);self.assertEqual(len(hashes),len(qa.FILES)+2)
            (app/qa.FILES[1]).write_bytes(b'wrong font')
            with self.assertRaisesRegex(ValueError,'asset'):qa.bundle_checks(root,app,'เชื่อมต่อผ่านเครือข่ายในพื้นที่')

    def test_english_or_thai_permission_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);app=root/'app';self.fixture(root,app)
            with self.assertRaisesRegex(ValueError,'Thai'):qa.bundle_checks(root,app,'English fallback')
            (app/'Info.plist').write_bytes(plistlib.dumps({'NSLocalNetworkUsageDescription':'Wrong scope'}))
            with self.assertRaisesRegex(ValueError,'English'):qa.bundle_checks(root,app,'เชื่อมต่อผ่านเครือข่ายในพื้นที่')


class FailureEvidence(unittest.TestCase):
    def test_cleanup_refuses_foreign_device_set(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);output=root/'artifacts';output.mkdir()
            (output/'result.json').write_text(json.dumps({'device':'foreign','simulator_set':'/elsewhere/global-simulators'}))
            with patch.object(qa.subprocess,'check_output') as command:
                with self.assertRaisesRegex(ValueError,'owned'):qa.cleanup(root,output)
                command.assert_not_called()

    def test_failed_native_smoke_still_cleans_owned_device_and_preserves_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);output=root/'artifacts/ios-localization';output.mkdir(parents=True)
            (output/'toolchain.json').write_text('{}');container=root/'container';(container/'Documents').mkdir(parents=True)
            calls=[]
            def run(parts,**kwargs):
                parts=list(map(str,parts));calls.append(parts);text=''
                if parts[:3]==['git','rev-parse','HEAD']:text='abc123'
                elif 'runtimes' in parts:text=json.dumps({'runtimes':[dict(identifier='com.apple.iOS-27',version='27',isAvailable=True)]})
                elif 'devicetypes' in parts:text=json.dumps({'devicetypes':[dict(identifier='phone',name='iPhone A')]})
                elif 'create' in parts:text='owned-uuid'
                elif 'get_app_container' in parts:text=str(container)
                elif 'mobile/ios_smoke.py' in parts:
                    self.assertEqual(kwargs['env']['SIMCTL_CHILD_SDL_PREFERRED_LOCALES'],'th_TH')
                    profile=Path(kwargs['env']['SIMCTL_CHILD_GLOB2_USER_DATA_DIR'])
                    self.assertNotIn('language=',(profile/'preferences.txt').read_text())
                    smoke=root/'build/mobile-smoke-ios-th';smoke.mkdir(parents=True);(smoke/'failure.png').write_bytes(b'image')
                    return qa.subprocess.CompletedProcess(parts,1,'smoke failed')
                return qa.subprocess.CompletedProcess(parts,0,text)
            with patch.object(qa.subprocess,'run',side_effect=run),patch.object(qa,'bundle_checks',return_value={}):
                with self.assertRaises(qa.subprocess.CalledProcessError):qa.run_qa(root,output,'th')
            actions=[p[-2:] for p in calls if p[-2:] in [['shutdown','owned-uuid'],['delete','owned-uuid']]]
            self.assertEqual(actions,[['shutdown','owned-uuid'],['delete','owned-uuid']])
            self.assertTrue((output/'smoke/failure.png').exists())
            result=json.loads((output/'result.json').read_text());self.assertFalse(result['passed'])
            smoke_call=next(p for p in calls if 'mobile/ios_smoke.py' in p)
            self.assertIn('--simulator-set',smoke_call)
            self.assertNotIn('shutdown all',' '.join(' '.join(p) for p in calls))


class MacBundleDependencies(unittest.TestCase):
    def test_localized_permission_files_invalidate_bundle(self):
        repo = next(p for p in Path(__file__).resolve().parents if (p/'SConstruct').is_file())
        sys.path.insert(0,str(repo/'scons'))
        module_path = ROOT/'scons/bundle.py'
        spec = importlib.util.spec_from_file_location('localization_bundle_contract',module_path)
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        class Env(dict):
            def Dir(self,p):return p
            def File(self,p):return p
            def Flatten(self,p):return p
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);plist=root/'Info.plist';plist.write_text('plist')
            thai=root/'th.lproj/InfoPlist.strings';thai.parent.mkdir();thai.write_text('purpose')
            english=root/'en.lproj/InfoPlist.strings';english.parent.mkdir();english.write_text('purpose')
            env=Env(BUNDLE_NAME='Glob2',BUNDLE_BINARIES=['binary'],BUNDLE_PLIST=str(plist))
            target,sources=module.bundleEmitter([],[],env)
            self.assertEqual(target,'Glob2.app')
            self.assertEqual(set(sources),{'binary',str(plist),str(thai),str(english)})


if __name__=='__main__':unittest.main()
