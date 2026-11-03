"""Portable contracts for the optional focused Windows localization dispatch."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'SConstruct').is_file())
WORKFLOW = Path(os.environ.get('GLOB2_WINDOWS_LOCALIZATION_WORKFLOW', ROOT / '.github/workflows/windows-generator-diagnostics.yml'))


def step(text, name):
    marker = '      - name: ' + name + '\n'
    tail = text.split(marker, 1)[1]
    return tail.split('\n      - ', 1)[0]


class WindowsLocalizationWorkflow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = WORKFLOW.read_text(encoding='utf-8')

    def test_optional_input_and_existing_revision_checkout(self):
        self.assertRegex(self.text, r'desktop_localization:\n        description: [^\n]+\n        required: false\n        type: boolean\n        default: false')
        self.assertIn('ref: ${{ inputs.revision || github.sha }}', self.text)
        self.assertEqual(self.text.count('GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix scons '), 1)
        self.assertIn('GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS: "1"', self.text)

    @unittest.skipUnless(shutil.which("bash"), "Workflow shell contract requires Bash (MSYS2 on Windows)")
    def test_actual_shell_target_selection_preserves_default(self):
        build = step(self.text, 'Build glob2 and the regression harnesses')
        selection = re.search(r'          preview_target=""\n.*?scons[^\n]+', build, re.S).group(0)
        selection = '\n'.join(line.strip() for line in selection.splitlines())
        # Shell functions avoid executable-bit/temporary-path differences on
        # Windows and do not require the nonstandard nproc command on macOS.
        stubs = 'scons() { printf "%s\\n" "$GLOB2_SDL3_PREFIX" "$@"; }; nproc() { printf "4\\n"; };\n'
        for mode in ('', 'false', 'true'):
            with self.subTest(mode=mode):
                command = stubs + selection.replace('${{ inputs.desktop_localization }}', mode)
                result = subprocess.run(['bash', '-e', '-c', command], text=True, capture_output=True, check=True)
                args = result.stdout.splitlines()
                self.assertEqual(args[:2], ['build/sdl3-ci/prefix', '-j4'])
                expected = ['release=1', 'mingw=1', 'server=0', 'engine-tests']
                if mode == 'true': expected.extend(['translation-preview-harness', 'unit-tests', 'build/mingw/client/release/src/glob2.exe'])
                self.assertEqual(args[2:], expected)

    def test_filters_belong_to_the_requested_binary_and_preview_alias_exists(self):
        spec = importlib.util.spec_from_file_location('native_test_registry', ROOT / 'test/tests.py')
        registry = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(registry)
        expected = {
            'ENGINE_TESTS': {'FontKerning': '#libs/gag/src/FontKerningTest.cpp', 'SystemLanguage': '#src/app/SystemLanguageTest.cpp'},
            'UNIT_TESTS': {'UILayout': '#libs/gag/src/ui/UILayoutHarness.cpp', 'BidiText': '#libs/gag/src/BidiTextTest.cpp', 'TextMetrics': '#libs/gag/src/FontMetricsTest.cpp'},
        }
        for binary, suites in expected.items():
            entries = {entry if isinstance(entry, str) else entry[0]: {} if isinstance(entry, str) else entry[1]
                       for entry in getattr(registry, binary)}
            for suite, source in suites.items():
                with self.subTest(binary=binary, suite=suite):
                    self.assertIn(source, entries)
                    self.assertNotIn('not-mingw', entries[source].get('require', set()))
                    self.assertRegex((ROOT / source[1:]).read_text(encoding='utf-8'),
                                     r'(?:TEST_SUITE|test_suite)\s*\(\s*"' + suite + '"')
        self.assertIn(('TranslationPreviewHarness', '#tools/TranslationPreviewHarness.cpp', 'translation-preview-harness', 'tools'), registry.PROGRAMS)

    def test_generator_inventory_only_disabled_by_explicit_mode(self):
        for name in ('Reproduce optional generator teardown', 'Capture teardown leaks and live stack'):
            body = step(self.text, name)
            self.assertIn("if: ${{ !cancelled() && !inputs.desktop_localization && steps.build_glob2_and_the_regression_harnesses.outcome == 'success' }}", body)
        replay = step(self.text, 'Reproduce optional generator teardown')
        self.assertIn("--filter 'ScriptGenerator/*' --jobs 1 --timeout 180", replay)
        self.assertIn('--junit artifacts/tests/engine-junit.xml', replay)
        crash = step(self.text, 'Capture teardown leaks and live stack')
        self.assertIn('--binary build/mingw/client/release/test/glob2-engine-tests.exe', crash)
        self.assertIn('--junit artifacts/tests/engine-junit.xml --output artifacts/tests/crash-diagnostics', crash)

    def test_capture_checks_and_artifacts_are_explicit_and_failure_preserving(self):
        for name in ('Check the actual Windows release client', 'Check focused native layout bidi and text metrics cases', 'Check focused native font layout and system language cases', 'Capture actual registered Thai menus and gameplay on Windows'):
            self.assertIn("if: ${{ !cancelled() && inputs.desktop_localization && steps.build_glob2_and_the_regression_harnesses.outcome == 'success' }}", step(self.text, name))
        checks = step(self.text, 'Check focused native font layout and system language cases')
        self.assertEqual(re.findall(r"--filter '([^']+)'", checks), ['FontKerning/*', 'SystemLanguage/*'])
        self.assertIn('--binary engine --build-dir build/mingw/client/release', checks)
        self.assertIn('env -u GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS', checks)
        unit = step(self.text, 'Check focused native layout bidi and text metrics cases')
        self.assertIn('--binary unit --build-dir build/mingw/client/release', unit)
        self.assertEqual(re.findall(r"--filter '([^']+)'", unit), ['UILayout/*', 'BidiText/*', 'TextMetrics/*'])
        capture = step(self.text, 'Capture actual registered Thai menus and gameplay on Windows')
        self.assertIn('python3 tools/capture_localization_platform.py --binary build/mingw/client/release/test/TranslationPreviewHarness.exe --output artifacts/localization-windows', capture)
        upload = step(self.text, 'Retain Windows localization evidence')
        self.assertIn('if: ${{ always() && inputs.desktop_localization }}', upload)
        self.assertIn('!artifacts/localization-windows/profiles/**', upload)
        self.assertIn('artifacts/localization-windows-tests/', upload)
        self.assertIn('if-no-files-found: error', upload)
        client = step(self.text, 'Check the actual Windows release client')
        self.assertIn('build/mingw/client/release/src/glob2.exe info version --format json', client)
        self.assertNotIn('continue-on-error', client + unit + checks + capture + upload)


if __name__ == '__main__': unittest.main()
