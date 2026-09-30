#!/usr/bin/env python3
"""Unit tests for test/run_tests.py: listing, selection, sharding and JUnit merging."""
import argparse
import os
import stat
import subprocess
import sys
import tempfile
import unittest
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

    def test_unit_binary_is_one_job(self):
        cases = run_tests.parse_listing(LISTING, 'unit')
        jobs = run_tests.make_jobs(cases, args())
        self.assertEqual(len(jobs), 1)
        self.assertTrue(jobs[0].whole)
        self.assertEqual(run_tests.doctest_filter(jobs[0]), [])
        subset = run_tests.make_jobs(cases[:2], args(), cases)
        self.assertEqual([job.subset for job in subset], [True, True])
        self.assertEqual(run_tests.doctest_filter(subset[0]), ['-tc=feeds the last worker', '-ts=HungryDefeat'])
        self.assertEqual(run_tests.doctest_filter(subset[1]),
                         ['-tc=renders the bar [display:1024x768][artifacts]', '-ts=PointBar'])
        self.assertFalse(run_tests.make_jobs(cases, args(), cases)[0].subset)

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
                              'echo "<testsuites><testsuite name=\\"x\\"><testcase classname=\\"x\\" name=\\"$name\\"/></testsuite></testsuites>" > "$out"\n'
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
                              'echo "<testsuites><testsuite name=\\"x\\"><testcase classname=\\"test/HungryDefeatHarness.cpp\\" name=\\"$name\\"/></testsuite></testsuites>" > "$out"\n'
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
