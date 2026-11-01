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


class LocalizedResourcePlacement(unittest.TestCase):
    def test_actual_xcode_nested_lproj_output_is_rejected_before_plutil(self):
        # Observed in run41252857300 commands.log/build.log and app.tar.gz:
        # Glob2.app/th.lproj/th.lproj/InfoPlist.strings, with no root strings.
        with tempfile.TemporaryDirectory() as temp:
            app=Path(temp)/'Glob2.app'
            nested=app/'th.lproj/th.lproj/InfoPlist.strings'
            nested.parent.mkdir(parents=True);nested.write_bytes(b'compiled plist')
            with self.assertRaisesRegex(ValueError,'th.lproj/th.lproj/InfoPlist.strings'):
                qa.localized_strings_path(app)
            correct=app/'th.lproj/InfoPlist.strings';nested.rename(correct)
            self.assertEqual(qa.localized_strings_path(app),correct)

    def test_xcode_localized_sources_use_resources_without_duplicate_locale(self):
        repo=next(p for p in Path(__file__).resolve().parents if (p/'SConstruct').is_file())
        sys.path.insert(0,str(repo/'mobile'))
        spec=importlib.util.spec_from_file_location('ios_packaging_contract',ROOT/'mobile/ios.py')
        generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory(prefix='ios resources ') as temp:
            root=Path(temp)
            for language in ['th','ja']:
                source=root/(language+'.lproj')/'InfoPlist.strings'
                source.parent.mkdir();source.write_text('"purpose" = "ข้อความ";',encoding='utf-8')
            lines=generator.localized_resource_lines(root)
            properties=[line for line in lines if line.startswith('set_source_files_properties')]
            self.assertEqual(len(properties),2)
            self.assertTrue(all(line.endswith('MACOSX_PACKAGE_LOCATION Resources)') for line in properties))
            self.assertTrue(any('th.lproj/InfoPlist.strings' in line for line in lines))
            self.assertFalse(any('Resources/th.lproj' in line or 'Resources/ja.lproj' in line for line in lines))


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
                if parts[:2] == ['xcrun','simctl']:
                    # Actual run41257767175: simctl rejected inventory with
                    # 'Provided set path does not exist'. Assert the precondition
                    # for every simulator call, including the first list runtimes.
                    self.assertTrue(Path(parts[parts.index('--set')+1]).is_dir())
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
            with patch.object(qa.subprocess,'run',side_effect=run),patch.object(qa,'bundle_checks',return_value={}),patch.object(qa,'localized_strings_path',return_value=root/'app/th.lproj/InfoPlist.strings'):
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
        module_path = repo/'scons/bundle.py'
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
