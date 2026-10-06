"""Coverage accounting regressions; no compiler or LLVM installation required."""
import importlib.util
import contextlib
import io
import sys
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('run_coverage', Path(__file__).with_name('run_coverage.py'))
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


def entry(path, count, covered):
    return {'filename': str(path), 'summary': {
        metric: {'count': count, 'covered': covered, 'percent': covered * 100 / count if count else 0}
        for metric in ('lines', 'branches', 'functions')}}


class CoverageSummaryTests(unittest.TestCase):
    def test_streaming_retains_output_and_reports_nonzero_exit_before_upload(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            released = directory / 'released'

            class LiveOutput(io.StringIO):
                def write(self, text):
                    if text.strip() == 'ready':
                        released.touch()
                    return super().write(text)

            output = LiveOutput()
            command = [sys.executable, '-c',
                       "import pathlib, sys, time; print('ready'); "
                       "p=pathlib.Path(sys.argv[1]); deadline=time.monotonic()+5; "
                       "exec('while not p.exists() and time.monotonic()<deadline: time.sleep(.01)'); "
                       "print('diagnostic', file=sys.stderr); sys.exit(7 if p.exists() else 9)",
                       str(released)]
            with contextlib.redirect_stdout(output):
                status = coverage.run_logged(command, directory / 'run.log', stream_logs=True)
            self.assertEqual(status, 7)
            self.assertEqual((directory / 'run.log').read_text(), 'ready\ndiagnostic\n')
            self.assertIn('exit=7', output.getvalue())
            self.assertIn('diagnostic', output.getvalue())

    def test_default_logging_retains_output_without_console_streaming(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / 'run.log'
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = coverage.run_logged([sys.executable, '-c',
                                              "print('retained'); raise SystemExit(3)"], log)
            self.assertEqual(status, 3)
            self.assertEqual(log.read_text(), 'retained\n')
            self.assertEqual(output.getvalue(), '')

    def test_failed_report_retains_all_raw_profiles(self):
        with tempfile.TemporaryDirectory() as temporary:
            profiles = [Path(temporary) / 'first.profraw', Path(temporary) / 'second.profraw']
            for path in profiles:
                path.write_bytes(b'raw profile evidence')
            self.assertEqual(coverage.compact_profiles(profiles, successful=False),
                             {'raw_profiles_retained': True})
            self.assertTrue(all(path.read_bytes() == b'raw profile evidence' for path in profiles))

    def test_completed_report_compacts_only_selected_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            profiles = [directory / 'first.profraw', directory / 'second.profraw']
            for path in profiles:
                path.write_bytes(b'raw')
            report = directory / 'coverage.profdata'
            report.write_bytes(b'merged evidence')
            other = directory / 'unmerged.profraw'
            other.write_bytes(b'failed run')
            self.assertEqual(coverage.compact_profiles(profiles, successful=True),
                             {'raw_profiles_retained': False, 'raw_profile_bytes_removed': 6})
            self.assertFalse(any(path.exists() for path in profiles))
            self.assertEqual(report.read_bytes(), b'merged evidence')
            self.assertEqual(other.read_bytes(), b'failed run')

    def test_counts_are_weighted_and_headers_are_not_implementation_lines(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = {'data': [{'files': [entry(root/'src/ai/castor/Small.cpp', 10, 10),
                                       entry(root/'src/ai/castor/Large.cpp', 90, 0),
                                       entry(root/'src/ai/castor/Inline.h', 100, 100)]}]}
            result = coverage.summarize(data, root)
            self.assertEqual(result['areas']['src/ai/castor']['lines'],
                             {'count': 100, 'covered': 10, 'percent': 10})
            self.assertEqual(len(result['files']), 3)

    def test_multiplayer_external_and_test_sources_are_excluded(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = [root/'src/net/Transport.cpp', root/'src/yog/Server.cpp', root/'test/Harness.cpp',
                     root/'third_party/vendor.cpp', root.parent/'external.cpp', root/'src/unit/Unit.cpp']
            result = coverage.summarize({'data': [{'files': [entry(p, 10, 5) for p in paths]}]}, root)
            self.assertEqual([f['path'] for f in result['files']], ['src/unit/Unit.cpp'])

    def test_unmeasured_sources_are_not_reported_as_zero_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('src/Measured.cpp','src/platform/Unbuilt.cpp','src/net/Excluded.cpp'):
                path = root/name
                path.parent.mkdir(parents=True,exist_ok=True)
                path.touch()
            result = coverage.summarize({'data': [{'files': [entry(root/'src/Measured.cpp',0,0)]}]},root)
            self.assertEqual(result['unmeasured_translation_units'], ['src/platform/Unbuilt.cpp'])
            self.assertIsNone(result['areas']['src']['lines']['percent'])

    def test_uncovered_functions_use_the_definition_file_and_exclude_headers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cpp, header = root/'src/AI.cpp', root/'src/AI.h'
            def function(name, count, file_id):
                return {'name': name, 'count': count, 'filenames': [str(header), str(cpp)],
                        'regions': [[12, 1, 20, 1, count, file_id, 0, 0]]}
            export = {'data': [{'files': [entry(cpp,10,5), entry(header,10,0)],
                                'functions': [function('missing',0,1), function('inline',0,0),
                                              function('covered',1,1)]}]}
            self.assertEqual(coverage.summarize(export,root)['uncovered_functions'],
                             [{'path':'src/AI.cpp','line':12,'name':'missing'}])

    def test_area_groups_preserve_ai_subsystems_and_other_roots(self):
        for path, expected in [('src/ai/cortex/CortexNet.cpp','src/ai/cortex'),
                               ('src/ai/AI.cpp','src/ai'),('src/SConscript.cpp','src'),
                               ('src/map/editor/Action.cpp','src/map/editor'),('libgag/src/Stream.cpp','libgag')]:
            with self.subTest(path=path):
                self.assertEqual(coverage.area(path),expected)


if __name__ == '__main__':
    unittest.main()
