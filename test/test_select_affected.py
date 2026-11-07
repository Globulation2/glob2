#!/usr/bin/env python3
"""Contracts for coverage-based test selection (test/select_affected.py, test/case_coverage.py)."""
import datetime as dt
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import case_coverage  # noqa: E402
import select_affected as selector  # noqa: E402

NOW = dt.datetime(2026, 11, 7, tzinfo=dt.timezone.utc)
MAP = {'schema': 1, 'sha': 'abc', 'created': '2026-11-06T00:00:00+00:00', 'cases': {
    'engine:Units/walk': ['src/unit/Unit.cpp', 'src/map/Map.cpp'],
    'engine:Hud/draw': ['src/hud/GameGUI.cpp'],
    'engine:TurnEngineHarness/the committed match': ['src/net/turn/Engine.cpp'],
}}
LABELS = sorted(MAP['cases']) + ['engine:New/case']


def choose(changes, coverage=MAP):
    return selector.select(LABELS, changes, coverage, now=NOW, is_ancestor=lambda sha: True)


class SelectionTest(unittest.TestCase):
    def test_intersection_plus_unrecorded_and_always_run_cases(self):
        chosen = choose(['src/unit/Unit.cpp']).cases
        self.assertEqual(chosen, {'engine:Units/walk', 'engine:New/case', 'engine:TurnEngineHarness/the committed match'})

    def test_documentation_only_change_runs_only_mandatory_cases(self):
        self.assertEqual(choose(['docs/README.md']).cases, {'engine:New/case', 'engine:TurnEngineHarness/the committed match'})

    def test_unattributable_changes_and_stale_maps_run_everything(self):
        for change in ('src/map/Map.h', 'SConstruct', 'test/support/HeadlessGame.cpp', 'content/data/x.json',
                       '.github/workflows/build.yml', 'src/SConscript', 'scons/sources.py', 'src/data.txt'):
            with self.subTest(change=change):
                self.assertIsNone(choose([change]).cases)
        self.assertIsNone(choose(['src/unit/Unit.cpp'], None).cases)
        old = dict(MAP, created='2026-10-01T00:00:00+00:00')
        self.assertIsNone(choose(['src/unit/Unit.cpp'], old).cases)
        self.assertIsNone(selector.select(LABELS, ['src/unit/Unit.cpp'], MAP, now=NOW, is_ancestor=lambda sha: False).cases)


class RecordingTest(unittest.TestCase):
    def test_parses_executed_functions(self):
        shown = ('Counters:\n  main:\n    Hash: 0x1\n    Counters: 2\n    Function count: 1\n'
                 '  _Z1gi:\n    Hash: 0x2\n    Counters: 1\n    Function count: 0\n'
                 'Instrumentation level: Front-end\nFunctions shown: 2\n')
        self.assertEqual(case_coverage.executed_functions(shown), ['main'])

    @unittest.skipUnless(shutil.which('clang++-18') and shutil.which('llvm-profdata-18') and shutil.which('llvm-cov-18'),
                         'needs clang++-18 and LLVM 18 tools')
    def test_records_and_maps_a_real_profile(self):
        with tempfile.TemporaryDirectory(dir=HERE.parent / 'artifacts' if (HERE.parent / 'artifacts').is_dir() else None) as directory:
            work = Path(directory)
            source = work / 'probe.cpp'
            source.write_text('int f(int x){return x+1;}\nint g(int x){return x*2;}\n'
                              'int main(int argc,char**){return argc>1?g(argc):f(argc);}\n')
            binary = work / 'probe'
            subprocess.run(['clang++-18', '-fprofile-instr-generate', '-fcoverage-mapping', str(source), '-o', str(binary)], check=True)
            profiles = work / 'profiles'
            profiles.mkdir()
            env = {'LLVM_PROFILE_FILE': case_coverage.job_environment(profiles, 0)}
            subprocess.run([str(binary)], env=env, check=False)
            case_coverage.finish_job(profiles, 0, ['engine:Probe/f'], 'llvm-profdata-18')
            # Job profiles are kept per writing binary, named by its build ID when it has one.
            written = sorted(profiles.glob('job-00000*.profdata'))
            self.assertEqual(len(written), 1)
            self.assertEqual(written[0].stem.partition('.')[2], case_coverage.binary_id_of_file(binary))
            mapping = case_coverage.function_files([binary], written[0], 'llvm-cov-18', root=work)
            self.assertEqual(mapping['_Z1fi'], ['probe.cpp'])


if __name__ == '__main__':
    unittest.main()
