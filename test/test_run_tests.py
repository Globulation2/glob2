#!/usr/bin/env python3
"""Unit tests for test/run_tests.py: listing, selection, sharding and JUnit merging."""
import argparse
import os
import stat
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_tests  # noqa: E402

LISTING = """<?xml version="1.0" encoding="UTF-8"?>
<doctest binary="x" version="2.4.11">
  <Options order_by="file"/>
  <TestCase name="feeds the last worker" testsuite="HungryDefeat" filename="test/HungryDefeatHarness.cpp" line="12" skipped="false"/>
  <TestCase name="renders the bar [display:1024x768][artifacts]" testsuite="PointBar" filename="test/PointBarRenderTest.cpp" line="40" skipped="false"/>
  <TestCase name="sweeps every landscape [slow]" testsuite="MapGeneratorDefaults" filename="test/MapGeneratorDefaultsTest.cpp" line="7" skipped="false"/>
  <TestCase name="binds a port [network]" testsuite="NetConnection" filename="test/NetConnectionHarness.cpp" line="9" skipped="false"/>
  <OverallResultsTestCases unskipped="4"/>
</doctest>
"""

FAKE_CLASSNAME_SHELL = (
    'case "$name" in\n'
    '"feeds the last worker") class="test/HungryDefeatHarness.cpp";;\n'
    '"sweeps every landscape [slow]") class="test/MapGeneratorDefaultsTest.cpp";;\n'
    '"binds a port [network]") class="test/NetConnectionHarness.cpp";;\n'
    '*) class="test/PointBarRenderTest.cpp";; esac\n'
)


def junit_for(cases, skipped=False):
    report = ET.Element('testsuites')
    suite = ET.SubElement(report, 'testsuite', tests='999')
    for case in cases:
        child = ET.SubElement(suite, 'testcase', classname=case.file, name=case.name)
        if skipped:
            ET.SubElement(child, 'skipped')
    return ET.tostring(report, encoding='unicode')


def args(**overrides):
    namespace = argparse.Namespace(filter=[], tag=[], exclude_tag=[], quick=False, no_display=False,
                                   in_process=False)
    for key, value in overrides.items():
        setattr(namespace, key, value)
    return namespace


class ListingTest(unittest.TestCase):
    def test_parses_names_suites_and_tags(self):
        cases = run_tests.parse_listing(LISTING, 'engine')
        self.assertEqual([case.label for case in cases][:2],
                         ['HungryDefeat/feeds the last worker', 'PointBar/renders the bar [display:1024x768][artifacts]'])
        self.assertEqual(cases[1].tags, ['display:1024x768', 'artifacts'])
        self.assertTrue(cases[1].display)
        self.assertEqual(cases[1].screen, '1024x768')
        self.assertEqual(cases[0].screen, run_tests.DEFAULT_SCREEN)
        self.assertEqual(cases[0].line, 12)


class SelectionTest(unittest.TestCase):
    def setUp(self):
        self.cases = run_tests.parse_listing(LISTING, 'engine')

    def test_filter_and_tags(self):
        kept, skipped = run_tests.select(self.cases, args(filter=['hungrydefeat/*']))
        self.assertEqual([case.suite for case in kept], ['HungryDefeat'])
        kept, _ = run_tests.select(self.cases, args(tag=['artifacts']))
        self.assertEqual([case.suite for case in kept], ['PointBar'] if os.name != 'nt' else [])
        kept, _ = run_tests.select(self.cases, args(exclude_tag=['display', 'slow', 'network']))
        self.assertEqual([case.suite for case in kept], ['HungryDefeat'])

    def test_quick_and_no_display(self):
        kept, skipped = run_tests.select(self.cases, args(quick=True, no_display=True))
        self.assertEqual(sorted(case.suite for case in kept), ['HungryDefeat', 'NetConnection'])
        self.assertEqual([case.suite for case in skipped], ['PointBar'])


class ShardTest(unittest.TestCase):
    def test_shards_are_deterministic_and_complete(self):
        cases = run_tests.parse_listing(LISTING, 'engine')
        jobs = run_tests.make_jobs(cases, args())
        parts = [run_tests.shard(jobs, f'{k}/3') for k in (1, 2, 3)]
        labels = sorted(job.label for part in parts for job in part)
        self.assertEqual(labels, sorted(job.label for job in jobs))
        self.assertEqual(parts, [run_tests.shard(jobs, f'{k}/3') for k in (1, 2, 3)])
        with self.assertRaises(SystemExit):
            run_tests.shard(jobs, '4/3')

    def test_unit_display_cases_are_isolated_from_headless_driver_changes(self):
        cases = run_tests.parse_listing(LISTING, 'unit')
        for in_process in (False, True):
            jobs = run_tests.make_jobs(cases, args(in_process=in_process), cases)
            self.assertEqual(len(jobs), 2)
            self.assertEqual(jobs[0].cases, [cases[1]])
            self.assertTrue(jobs[0].display)
            self.assertFalse(jobs[0].whole)
            self.assertTrue(jobs[1].whole)
            self.assertFalse(jobs[1].display)
            self.assertEqual(run_tests.doctest_filter(jobs[1]), ['-tce=*[display*'])
        subset = run_tests.make_jobs(cases[:2], args(), cases)
        self.assertEqual([job.subset for job in subset], [False, True])
        self.assertEqual(run_tests.doctest_filter(subset[1]),
                         ['-tc=feeds the last worker', '-ts=HungryDefeat'])
        benchmark = run_tests.Case('unit', '', 'timing [benchmark]', tags=['benchmark'])
        jobs = run_tests.make_jobs(cases, args(), cases + [benchmark])
        self.assertEqual(run_tests.doctest_filter(jobs[1]),
                         ['-tce=*[benchmark]*,*[display*'])
        headless = [case for case in cases if not case.display]
        jobs = run_tests.make_jobs(headless, args(no_display=True), cases)
        self.assertEqual(len(jobs), 1)
        self.assertEqual(run_tests.doctest_filter(jobs[0]), ['-tce=*[display*'])

    def test_same_name_in_two_suites_stays_in_its_suite(self):
        listing = LISTING.replace('<OverallResultsTestCases', '<TestCase name="feeds the last worker" testsuite="InnSwap" '
                                  'filename="test/InnSwapHarness.cpp" line="3" skipped="false"/>\n  <OverallResultsTestCases')
        cases = run_tests.parse_listing(listing, 'unit')
        kept, _ = run_tests.select(cases, args(filter=['InnSwap/*']))
        jobs = run_tests.make_jobs(kept, args(), cases)
        self.assertEqual(len(jobs), 1)
        self.assertEqual(run_tests.doctest_filter(jobs[0]), ['-tc=feeds the last worker', '-ts=InnSwap'])
        foreign = run_tests.Result(jobs[0], 'pass', 0.1, junit=(
            '<testsuites><testsuite name="x"><testcase classname="test/HungryDefeatHarness.cpp" name="feeds the last worker"/>'
            '<testcase classname="test/InnSwapHarness.cpp" name="feeds the last worker"/></testsuite></testsuites>'))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'junit.xml'
            run_tests.merge_junit([foreign], path)
            text = path.read_text()
        self.assertIn('classname="InnSwap"', text)
        self.assertNotIn('classname="InnSwap" name="feeds the last worker"/><testcase classname="InnSwap"', text)
        self.assertIn('classname="test/HungryDefeatHarness.cpp"', text)
        engine = run_tests.make_jobs(run_tests.parse_listing(LISTING, 'engine'), args())
        self.assertEqual(len(engine), 4)
        self.assertEqual(run_tests.doctest_filter(engine[0]), ['-tc=feeds the last worker', '-ts=HungryDefeat'])
        self.assertEqual(engine[2].timeout, run_tests.SLOW_TIMEOUT)
        self.assertEqual(engine[1].timeout, run_tests.DISPLAY_TIMEOUT)

    def test_filter_quotes_comma_and_backslash_in_names_and_suites(self):
        case = run_tests.Case('engine', r'Suite,with\path', r'cancel, globals\saved')
        second = run_tests.Case('engine', case.suite, 'second, case')
        self.assertEqual(run_tests.doctest_filter(run_tests.Job('engine', [case])),
                         [r'-tc=cancel\, globals\\saved', r'-ts=Suite\,with\\path'])
        self.assertEqual(run_tests.doctest_filter(run_tests.Job('engine', [case, second], whole=True, subset=True)),
                         [r'-tc=cancel\, globals\\saved,second\, case', r'-ts=Suite\,with\\path'])


class JunitExecutionTest(unittest.TestCase):
    def setUp(self):
        self.cases = run_tests.parse_listing(LISTING, 'engine')[:2]
        self.job = run_tests.Job('engine', self.cases, whole=True, subset=True)

    def test_complete_actual_cases_pass_regardless_of_reported_test_count(self):
        self.assertEqual(run_tests.junit_execution_issue(self.job, junit_for(self.cases)), '')

    def test_empty_missing_malformed_and_only_skipped_reports_fail(self):
        for report in ('', '<testsuites tests="999"/>', '<broken', junit_for(self.cases, skipped=True)):
            with self.subTest(report=report):
                self.assertTrue(run_tests.junit_execution_issue(self.job, report))

    def test_incomplete_duplicate_and_foreign_cases_fail(self):
        for cases in (self.cases[:1], [self.cases[0]] * 2,
                      [*self.cases, run_tests.Case('engine', 'Other', self.cases[0].name, 'other.cpp')]):
            with self.subTest(cases=cases):
                issue = run_tests.junit_execution_issue(self.job, junit_for(cases))
                self.assertIn('does not match selection', issue)

    def test_same_name_from_wrong_file_does_not_satisfy_selection(self):
        wrong = run_tests.Case('engine', 'Other', self.cases[0].name, 'other.cpp')
        issue = run_tests.junit_execution_issue(self.job, junit_for([wrong, self.cases[1]]))
        self.assertIn('missing selected cases', issue)
        self.assertIn('unexpected executed cases', issue)

    def test_failure_in_successful_exit_report_is_rejected(self):
        report = ET.fromstring(junit_for(self.cases))
        ET.SubElement(report.find('.//testcase'), 'failure', message='actual failure')
        self.assertIn('failures', run_tests.junit_execution_issue(self.job, ET.tostring(report, encoding='unicode')))


class JunitTest(unittest.TestCase):
    def test_merges_doctest_reports_and_synthesises_errors(self):
        cases = run_tests.parse_listing(LISTING, 'engine')
        jobs = run_tests.make_jobs(cases, args())
        passed = run_tests.Result(jobs[0], 'pass', 0.5, junit=(
            '<testsuites><testsuite name="test/HungryDefeatHarness.cpp"><testcase classname="test/HungryDefeatHarness.cpp"'
            ' name="feeds the last worker" time="0.4"/></testsuite></testsuites>'))
        crashed = run_tests.Result(jobs[1], 'timeout', 300.0, output='tail of output')
        skipped = run_tests.Result(jobs[2], 'skip', 0.0)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'junit.xml'
            totals = run_tests.merge_junit([passed, crashed, skipped], path)
            text = path.read_text()
        self.assertEqual(totals, {'tests': 3, 'failures': 0, 'errors': 1, 'skipped': 1})
        self.assertIn('classname="HungryDefeat"', text)
        self.assertIn('<error message="timeout"', text)
        self.assertIn('tail of output', text)
        self.assertIn('<skipped', text)


@unittest.skipIf(os.name == 'nt', 'uses a shell script as the fake binary')
class EndToEndTest(unittest.TestCase):
    def test_fullscreen_requires_explicit_opt_in_even_in_process(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / 'build'
            (build / 'test').mkdir(parents=True)
            binary = build / 'test' / run_tests.BINARIES['engine']
            listing = LISTING.replace('\n', '\\n').replace('"', '\\"')
            binary.write_text('#!/bin/sh\n'
                              'for a in "$@"; do case "$a" in -ltc) printf "%b" "' + listing + '"; exit 0;; esac; done\n'
                              'test "$GLOB2_TEST_FULLSCREEN" = "$EXPECTED_FULLSCREEN" || exit 9\n'
                              'for a in "$@"; do case "$a" in -o=*) echo "<testsuites><testsuite><testcase classname=\\"test/HungryDefeatHarness.cpp\\" name=\\"feeds the last worker\\"/></testsuite></testsuites>" > "${a#-o=}";; esac; done\n')
            binary.chmod(binary.stat().st_mode | stat.S_IEXEC)
            for in_process in (False, True):
                for fullscreen in (False, True):
                    with self.subTest(in_process=in_process, fullscreen=fullscreen):
                        command = [sys.executable, str(HERE / 'run_tests.py'), '--binary', 'engine',
                                   '--build-dir', str(build), '--filter', 'HungryDefeat/*',
                                   '--junit', str(Path(directory) / 'junit.xml')]
                        if in_process:
                            command.append('--in-process')
                        if fullscreen:
                            command.append('--fullscreen')
                        result = subprocess.run(command, capture_output=True, text=True,
                                                env=dict(os.environ, GLOB2_TEST_FULLSCREEN='1',
                                                         EXPECTED_FULLSCREEN='1' if fullscreen else '0'))
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def run_python_fake(self, cases, report, extra=(), required_filters=()):
        listing = ET.Element('doctest')
        for case in cases:
            ET.SubElement(listing, 'TestCase', name=case.name, testsuite=case.suite,
                          filename=case.file, line='1', skipped='false')
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / 'build'
            (build / 'test').mkdir(parents=True)
            binary = build / 'test' / run_tests.BINARIES['engine']
            binary.write_text(
                f'#!{sys.executable}\n'
                'import sys\nfrom pathlib import Path\n'
                f'listing = {ET.tostring(listing, encoding="unicode")!r}\n'
                'if "-ltc" in sys.argv:\n print(listing); raise SystemExit(0)\n'
                f'report = {report!r}\n'
                f'if not all(value in sys.argv for value in {tuple(required_filters)!r}): report = "<testsuites/>"\n'
                'output = next(value[3:] for value in sys.argv if value.startswith("-o="))\n'
                'Path(output).write_text(report)\n')
            binary.chmod(binary.stat().st_mode | stat.S_IEXEC)
            junit = Path(directory) / 'junit.xml'
            completed = subprocess.run([sys.executable, str(HERE / 'run_tests.py'), '--binary', 'engine',
                                        '--build-dir', str(build), '--no-display', '--junit', str(junit),
                                        '--artifacts', str(Path(directory) / 'artifacts'), *extra],
                                       capture_output=True, text=True)
            return completed, junit.read_text()

    def test_separator_names_run_instead_of_silently_selecting_nothing(self):
        case = run_tests.Case('engine', r'Suite,with\path', r'cancel, globals\saved', 'fixture.cpp')
        completed, report = self.run_python_fake(
            [case], junit_for([case]),
            required_filters=(r'-tc=cancel\, globals\\saved', r'-ts=Suite\,with\\path'))
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertIn('PASS', completed.stdout)
        self.assertEqual(len(ET.fromstring(report).findall('.//testcase')), 1)

    def test_successful_process_with_empty_or_partial_junit_is_an_error(self):
        cases = [run_tests.Case('engine', 'Runner', 'first', 'first.cpp'),
                 run_tests.Case('engine', 'Runner', 'second', 'second.cpp')]
        for report, diagnostic in (('<testsuites tests="999"/>', 'no executed test cases'),
                                   (junit_for(cases[:1]), 'missing selected cases')):
            with self.subTest(report=report):
                completed, merged = self.run_python_fake(cases, report, extra=('--in-process',))
                self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
                self.assertIn(diagnostic, completed.stdout)
                self.assertEqual(len(ET.fromstring(merged).findall('.//testcase/error')), 2)

    def test_runs_a_fake_binary_per_case(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / 'build'
            (build / 'test').mkdir(parents=True)
            binary = build / 'test' / run_tests.BINARIES['engine']
            listing = LISTING.replace('\n', '\\n').replace('"', '\\"')
            binary.write_text('#!/bin/sh\n'
                              'for a in "$@"; do case "$a" in -ltc) printf "%b" "' + listing + '"; exit 0;; esac; done\n'
                              'out=""; name=""\n'
                              'for a in "$@"; do case "$a" in -o=*) out="${a#-o=}";; -tc=*) name="${a#-tc=}";; esac; done\n'
                              'echo "running $name"\n'
                              'test -n "$GLOB2_USER_DATA_DIR" || exit 9\n'
                              'if [ "$name" = "binds a port [network]" ]; then echo "<testsuites/>" > "$out"; echo boom; exit 1; fi\n'
                              + FAKE_CLASSNAME_SHELL +
                              'echo "<testsuites><testsuite name=\\"x\\"><testcase classname=\\"$class\\" name=\\"$name\\"/></testsuite></testsuites>" > "$out"\n'
                              'exit 0\n')
            binary.chmod(binary.stat().st_mode | stat.S_IEXEC)
            junit = Path(directory) / 'junit.xml'
            result = subprocess.run([sys.executable, str(HERE / 'run_tests.py'), '--binary', 'engine',
                                     '--build-dir', str(build), '--no-display', '--junit', str(junit),
                                     '--artifacts', str(Path(directory) / 'artifacts'), '-j', '2'],
                                    capture_output=True, text=True, env=dict(os.environ, GITHUB_ACTIONS='1'))
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn('FAIL NetConnection/binds a port [network]', result.stdout)
            self.assertIn('PASS HungryDefeat/feeds the last worker', result.stdout)
            self.assertIn('boom', result.stdout)
            self.assertIn('::error file=test/NetConnectionHarness.cpp,line=9', result.stdout)
            self.assertIn('2 passed, 1 failed, 1 skipped', result.stdout)
            self.assertTrue(junit.exists())

    def test_reports_preference_writes_and_timeouts(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / 'build'
            (build / 'test').mkdir(parents=True)
            binary = build / 'test' / run_tests.BINARIES['engine']
            listing = LISTING.replace('\n', '\\n').replace('"', '\\"')
            binary.write_text('#!/bin/sh\n'
                              'for a in "$@"; do case "$a" in -ltc) printf "%b" "' + listing + '"; exit 0;; esac; done\n'
                              'out=""; name=""\n'
                              'for a in "$@"; do case "$a" in -o=*) out="${a#-o=}";; -tc=*) name="${a#-tc=}";; esac; done\n'
                              'if [ "$name" = "sweeps every landscape [slow]" ]; then sleep 30; fi\n'
                              'if [ "$name" = "feeds the last worker" ]; then echo "musicVolume=3" > "$GLOB2_USER_DATA_DIR/preferences.txt"; fi\n'
                              + FAKE_CLASSNAME_SHELL +
                              'echo "<testsuites><testsuite name=\\"x\\"><testcase classname=\\"$class\\" name=\\"$name\\"/></testsuite></testsuites>" > "$out"\n'
                              'exit 0\n')
            binary.chmod(binary.stat().st_mode | stat.S_IEXEC)
            junit = Path(directory) / 'junit.xml'
            result = subprocess.run([sys.executable, str(HERE / 'run_tests.py'), '--binary', 'engine',
                                     '--build-dir', str(build), '--no-display', '--junit', str(junit),
                                     '--artifacts', str(Path(directory) / 'artifacts'), '-j', '2', '--timeout', '2'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn('FAIL HungryDefeat/feeds the last worker', result.stdout)
            self.assertIn('changed the profile preferences', result.stdout)
            self.assertIn('TIMEOUT MapGeneratorDefaults/sweeps every landscape [slow]', result.stdout)
            self.assertIn('killed after 2s', result.stdout)
            self.assertIn('PASS NetConnection/binds a port [network]', result.stdout)
            text = junit.read_text()
            self.assertIn('<failure message="run_tests: the test changed the profile preferences', text)
            self.assertIn('<error message="timeout"', text)


if __name__ == '__main__':
    unittest.main()
