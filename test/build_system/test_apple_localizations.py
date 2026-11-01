"""Native Apple permission resources reach bundles with an English fallback.

These tests execute packaging/resource generation on Linux; they do not exercise
Apple's language selection or permission UI.
"""
import contextlib
import ast
import importlib.util
import json
from pathlib import Path
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'mobile'))
sys.path.insert(0, str(ROOT / 'scons'))
import ios
from apple_localizations import localization_files


def strings(path):
    text = Path(path).read_text(encoding='utf-8')
    entries = re.findall(r'("(?:\\.|[^"\\])*")\s*=\s*("(?:\\.|[^"\\])*")\s*;', text)
    return {json.loads(key): json.loads(value) for key, value in entries}


class AppleLocalizationTests(unittest.TestCase):
    def fixture(self, root):
        directories = [root / 'en.lproj', root / 'mr.lproj', root / 'ta.lproj']
        for directory, value in zip(directories, ('English purpose.', 'मराठी चाचणी.', 'தமிழ் சோதனை.')):
            directory.mkdir(parents=True)
            (directory / 'InfoPlist.strings').write_text(
                '"NSLocalNetworkUsageDescription" = ' + json.dumps(value, ensure_ascii=False) + ';\n', encoding='utf-8')
        return directories

    def test_macos_bundle_copies_declared_localizations_and_tracks_them(self):
        if importlib.util.find_spec('SCons') is None:
            self.skipTest('SCons is required for the macOS bundle-copy contract')
        import bundle
        from SCons.Script import Environment
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directories = self.fixture(root)
            for name in ('COPYING', 'data/javascript-licenses.txt', 'data/json-license.txt',
                         'docs/assets/source-attribution.md', 'Info.plist', 'Glob2.icns'):
                file = root / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text(name)
            env = Environment(tools=[])
            env.Replace(BUNDLE_NAME=str(root / 'Glob2'), BUNDLE_BINARIES=[],
                        BUNDLE_RESOURCEDIRS=['data', *map(str, directories)],
                        BUNDLE_PLIST=str(root / 'Info.plist'), BUNDLE_ICON=str(root / 'Glob2.icns'),
                        BUILDDIR=str(root / 'build'), LEAN_IMAGE_PREFIX='fixture', VERSION='1')
            _, sources = bundle.bundleEmitter([], [], env)
            self.assertTrue({str(file.resolve()) for file in localization_files(directories)} <=
                            {str(node.abspath) for node in sources})
            self.assertIn(str((root / 'Info.plist').resolve()), {str(node.abspath) for node in sources})

            def platform_command(command):
                argv = shlex.split(command)
                if argv[0] == 'rm': shutil.rmtree(argv[-1], ignore_errors=True)
                elif argv[0] == 'mkdir': Path(argv[-1]).mkdir(parents=True, exist_ok=True)
                elif argv[0] == 'cp': shutil.copy2(argv[1], argv[2])
                elif argv[0] not in ('codesign', '/usr/libexec/PlistBuddy'):
                    self.fail('Unexpected platform command: ' + command)

            def export(source, destination, **kwargs):
                (destination / 'data').mkdir(parents=True)
                (destination / 'data/test.txt').write_text('exported game data')

            with contextlib.chdir(root), patch.object(bundle, 'run', side_effect=platform_command), \
                 patch.object(bundle, 'GetOption', return_value=1), \
                 patch.object(bundle, 'addDependentLibsToBundle'), \
                 patch('moltenvk.install_macos'), patch('tools.package_assets.export_assets', side_effect=export):
                bundle.createBundle([], [], env)
            resources = root / 'Glob2.app/Contents/Resources'
            for directory in directories:
                self.assertEqual((resources / directory.name / 'InfoPlist.strings').read_bytes(),
                                 (directory / 'InfoPlist.strings').read_bytes())
            self.assertEqual((resources / 'data/test.txt').read_text(), 'exported game data')

    def test_missing_declared_localization_is_not_silently_omitted(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, 'Missing Apple localization'):
                ios.localized_resource_lines(Path(temporary))

    @unittest.skipUnless(shutil.which('cmake'), 'CMake is required for the generator contract')
    def test_ios_generated_cmake_preserves_language_subdirectories(self):
        with tempfile.TemporaryDirectory(prefix='apple resources ') as temporary:
            root = Path(temporary)
            directories = self.fixture(root)
            # A language-free target parses the real generated resource commands
            # on Linux, without pretending to build an Apple app or probe an SDK.
            lines = ['cmake_minimum_required(VERSION 3.24)', 'project(Resources LANGUAGES NONE)',
                     'add_library(Glob2 INTERFACE)', *ios.localized_resource_lines(root)]
            for index, directory in enumerate(directories):
                resource = directory / 'InfoPlist.strings'
                lines += ['get_source_file_property(location ' + ios.cmake_quote(resource) + ' MACOSX_PACKAGE_LOCATION)',
                          'file(WRITE ' + ios.cmake_quote(root / (str(index) + '.txt')) + ' "${location}")']
            lines += ['get_target_property(sources Glob2 SOURCES)',
                      'file(WRITE ' + ios.cmake_quote(root / 'sources.txt') + ' "${sources}")']
            (root / 'CMakeLists.txt').write_text('\n'.join(lines) + '\n')
            subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build')],
                           check=True, capture_output=True, text=True)
            self.assertEqual((root / '0.txt').read_text(), 'Resources/en.lproj')
            self.assertEqual((root / '1.txt').read_text(), 'Resources/mr.lproj')
            self.assertEqual((root / '2.txt').read_text(), 'Resources/ta.lproj')
            self.assertEqual(set((root / 'sources.txt').read_text().split(';')),
                             {str(directory / 'InfoPlist.strings') for directory in directories})

    def test_shipped_sources_keep_english_development_fallback_and_key_parity(self):
        # Read the real release bundle declaration, rather than only the test's
        # synthetic environment, so dropping a locale from SConstruct fails.
        declared = []
        for node in ast.walk(ast.parse((ROOT / 'SConstruct').read_text())):
            if isinstance(node, ast.Call):
                for keyword in node.keywords:
                    if keyword.arg == 'BUNDLE_RESOURCEDIRS':
                        declared.extend(ast.literal_eval(keyword.value))
        self.assertTrue({'darwin/en.lproj', 'darwin/mr.lproj', 'darwin/ta.lproj'} <= set(declared))
        for base, plist_path in ((ROOT / 'darwin', ROOT / 'darwin/Info.plist'),
                                 (ROOT / 'mobile/ios', ROOT / 'mobile/ios/Info.plist.in')):
            with self.subTest(platform=base):
                info = plistlib.loads(plist_path.read_bytes())
                self.assertEqual(info['CFBundleDevelopmentRegion'], 'en')
                english = strings(base / 'en.lproj/InfoPlist.strings')
                marathi = strings(base / 'mr.lproj/InfoPlist.strings')
                tamil = strings(base / 'ta.lproj/InfoPlist.strings')
                self.assertEqual(set(english), {'NSLocalNetworkUsageDescription'})
                self.assertEqual(set(marathi), set(english))
                self.assertEqual(set(tamil), set(english))
                self.assertEqual(english['NSLocalNetworkUsageDescription'], info['NSLocalNetworkUsageDescription'])
                self.assertRegex(marathi['NSLocalNetworkUsageDescription'], r'[\u0900-\u097f]')
                self.assertIn('Globulation 2', marathi['NSLocalNetworkUsageDescription'])
                self.assertNotEqual(marathi, english)
                self.assertRegex(tamil['NSLocalNetworkUsageDescription'], r'[\u0b80-\u0bff]')
                self.assertIn('Globulation 2', tamil['NSLocalNetworkUsageDescription'])
                self.assertNotEqual(tamil, english)


if __name__ == '__main__':
    unittest.main()
