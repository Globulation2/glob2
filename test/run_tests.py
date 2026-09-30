#!/usr/bin/env python3
"""Run the native doctest binaries: glob2-engine-tests and glob2-unit-tests.

Every engine test case runs in its own process with a disposable profile, dummy SDL
drivers unless it is tagged [display], a timeout and captured output shown only on
failure. The unit binary runs in one process. Results are merged into one JUnit file
and, under GitHub Actions, into the step summary with per-failure annotations.

Tags are bracketed words at the end of a test-case name:
  [display]        needs a real window; xvfb on Linux, skipped on Windows and --no-display
  [display:WxH]    same, with a specific virtual screen size
  [slow]           timeout 600 s; excluded by --quick
  [network]        binds loopback sockets; never runs in parallel with another [network]
  [artifacts]      writes files for review under --artifacts
  [golden]         compares against checked-in text; --update-fixtures rewrites it
  [writes-preferences]  legitimately saves settings (the preferences check is skipped)
  other tags ([maxima], [save-format], ...) only select.

Examples:
  python3 test/run_tests.py                       # everything the platform can run
  python3 test/run_tests.py --list --tag display
  python3 test/run_tests.py --filter 'HungryDefeat/*' --verbose
  python3 test/run_tests.py --binary engine --shard 2/4 --junit artifacts/tests/junit.xml
  python3 test/run_tests.py --binary engine --in-process   # fast local loop
"""

import argparse
import concurrent.futures
import fnmatch
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_paths import native_build_directory  # noqa: E402

BINARIES = {'engine': 'glob2-engine-tests', 'unit': 'glob2-unit-tests'}
DEFAULT_TIMEOUT = 120
DISPLAY_TIMEOUT = 300
SLOW_TIMEOUT = 600
DEFAULT_SCREEN = '1600x1400'
TAG_PATTERN = re.compile(r'\[([^\]]+)\]')
PREFERENCES_MARKER = 'rememberUnit=1\n'


@dataclass
class Case:
    binary: str
    suite: str
    name: str
    file: str = ''
    line: int = 0
    tags: list = field(default_factory=list)

    @property
    def label(self):
        return f'{self.suite}/{self.name}' if self.suite else self.name

    @property
    def display(self):
        return any(tag.split(':')[0] == 'display' for tag in self.tags)

    @property
    def screen(self):
        for tag in self.tags:
            if tag.startswith('display:'):
                return tag.split(':', 1)[1]
        return DEFAULT_SCREEN

    def has(self, tag):
        return any(t == tag or t.startswith(tag + ':') for t in self.tags)


@dataclass
class Job:
    """One subprocess: a single engine case, or a whole binary run in-process."""
    binary: str
    cases: list
    whole: bool = False

    @property
    def label(self):
        return f'{BINARIES[self.binary]} ({len(self.cases)} cases)' if self.whole else self.cases[0].label

    @property
    def display(self):
        return any(case.display for case in self.cases)

    @property
    def screen(self):
        return self.cases[0].screen if not self.whole else DEFAULT_SCREEN

    def has(self, tag):
        return any(case.has(tag) for case in self.cases)

    @property
    def timeout(self):
        if self.whole:
            return SLOW_TIMEOUT * 2
        if self.has('slow'):
            return SLOW_TIMEOUT
        if self.display:
            return DISPLAY_TIMEOUT
        return DEFAULT_TIMEOUT


@dataclass
class Result:
    job: Job
    status: str          # pass | fail | error | timeout | skip
    seconds: float
    output: str = ''
    junit: str = ''      # the job's own JUnit document, when it produced one
    profile: str = ''


def parse_tags(name):
    return TAG_PATTERN.findall(name)


def binary_path(build_dir, kind):
    return build_dir / 'test' / (BINARIES[kind] + ('.exe' if os.name == 'nt' else ''))


def list_cases(binary, kind):
    completed = subprocess.run([str(binary), '-r=xml', '-ltc'], capture_output=True, text=True, cwd=ROOT)
    if completed.returncode:
        raise SystemExit(f'{binary} could not list its test cases:\n{completed.stderr}{completed.stdout}')
    return parse_listing(completed.stdout, kind)


def parse_listing(xml_text, kind):
    cases = []
    for element in ET.fromstring(xml_text).iter('TestCase'):
        name = element.get('name', '')
        cases.append(Case(kind, element.get('testsuite', ''), name, element.get('filename', ''),
                          int(element.get('line', '0') or 0), parse_tags(name)))
    return cases


def select(cases, args):
    """Apply --filter/--tag/--exclude-tag/--quick/--no-display; returns (kept, skipped)."""
    kept, skipped = [], []
    display_ok = not args.no_display and not (os.name == 'nt')
    for case in cases:
        if args.filter and not any(fnmatch.fnmatchcase(case.label.lower(), pattern.lower()) for pattern in args.filter):
            continue
        if args.tag and not all(case.has(tag) for tag in args.tag):
            continue
        if any(case.has(tag) for tag in args.exclude_tag):
            continue
        if args.quick and case.has('slow'):
            continue
        if case.display and not display_ok:
            skipped.append(case)
            continue
        kept.append(case)
    return kept, skipped


def shard(items, spec):
    if not spec:
        return items
    k, n = (int(part) for part in spec.split('/'))
    if not 1 <= k <= n:
        raise SystemExit(f'--shard {spec}: expected K/N with 1 <= K <= N')
    ordered = sorted(items, key=lambda item: item.label)
    return [item for index, item in enumerate(ordered) if index % n == k - 1]


def make_jobs(cases, args):
    jobs = []
    for kind in ('unit', 'engine'):
        mine = [case for case in cases if case.binary == kind]
        if not mine:
            continue
        if kind == 'unit' or args.in_process:
            jobs.append(Job(kind, mine, whole=True))
        else:
            jobs += [Job(kind, [case]) for case in mine]
    return jobs


def doctest_filter(job):
    if job.whole:
        return []
    case = job.cases[0]
    filters = ['-tc=' + case.name]
    if case.suite:
        filters.append('-ts=' + case.suite)
    return filters


def xvfb_prefix(job):
    if not job.display or platform.system() != 'Linux' or os.environ.get('DISPLAY'):
        return []
    xvfb = shutil.which('xvfb-run')
    if not xvfb:
        return []
    return [xvfb, '-a', '-s', f'-screen 0 {job.screen}x24']


def run_job(job, args, build_dir):
    binary = binary_path(build_dir, job.binary)
    keep = args.keep_profiles
    root = Path(tempfile.mkdtemp(prefix='glob2-test-'))
    profile, work, home = root / 'profile', root / 'work', root / 'home'
    for directory in (profile, work, home):
        directory.mkdir()
    preferences = profile / 'preferences.txt'
    preferences.write_text(PREFERENCES_MARKER)
    before = (preferences.read_bytes(), preferences.stat().st_mtime_ns)
    junit = root / 'junit.xml'
    env = dict(os.environ)
    env.update(GLOB2_USER_DATA_DIR=str(profile), HOME=str(home), USERPROFILE=str(home),
               TMPDIR=str(root), TMP=str(root), TEMP=str(root), SDL_AUDIODRIVER='dummy')
    env.pop('GLOB2_MOBILE_UI', None)
    env.pop('GLOB2_USER_DIR', None)
    if job.display:
        env['GLOB2_TEST_DISPLAY'] = '1'
        env.pop('SDL_VIDEODRIVER', None)
        if platform.system() == 'Linux':
            env.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
    else:
        env['SDL_VIDEODRIVER'] = 'dummy'
    if args.update_fixtures:
        env['GLOB2_TEST_UPDATE_FIXTURES'] = '1'
    if args.artifacts:
        artifacts = Path(args.artifacts).resolve()
        if not job.whole:
            case = job.cases[0]
            artifacts = artifacts / sanitized(case.suite or 'no-suite') / sanitized(case.name)
        env['GLOB2_TEST_ARTIFACTS'] = str(artifacts)
    command = xvfb_prefix(job) + [str(binary), '-r=junit', f'-o={junit}', '--no-breaks=true'] + doctest_filter(job)
    if args.verbose and job.whole:
        command += ['-s']
    started = time.monotonic()
    status, output = 'pass', ''
    with open(root / 'output.txt', 'w+', encoding='utf-8', errors='replace') as capture:
        popen_kwargs = dict(cwd=work, env=env, stdout=capture, stderr=subprocess.STDOUT)
        if os.name != 'nt':
            popen_kwargs['start_new_session'] = True
        process = subprocess.Popen(command, **popen_kwargs)
        try:
            code = process.wait(timeout=job.timeout)
        except subprocess.TimeoutExpired:
            terminate(process)
            code = None
        capture.seek(0)
        output = capture.read()
    seconds = time.monotonic() - started
    if code is None:
        status = 'timeout'
        output += f'\n[run_tests] killed after {job.timeout}s\n'
    elif code != 0:
        status = 'fail' if junit.exists() and code == 1 else 'error'
        if status == 'error':
            output += f'\n[run_tests] exit status {code}\n'
    if status == 'pass' and not job.has('writes-preferences') and not job.whole:
        after = (preferences.read_bytes(), preferences.stat().st_mtime_ns) if preferences.exists() else None
        if after != before:
            status = 'fail'
            output += '\n[run_tests] the test changed the profile preferences; tag it [writes-preferences] if that is intended\n'
    junit_text = junit.read_text(encoding='utf-8', errors='replace') if junit.exists() else ''
    result = Result(job, status, seconds, output, junit_text, str(root))
    if not keep:
        shutil.rmtree(root, ignore_errors=True)
    return result


def terminate(process):
    try:
        if os.name == 'nt':
            subprocess.run(['taskkill', '/T', '/F', '/PID', str(process.pid)], capture_output=True)
        else:
            os.killpg(process.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass


def sanitized(text):
    return re.sub(r'[^A-Za-z0-9._-]+', '_', text).strip('_') or 'unnamed'


def merge_junit(results, path):
    suites = ET.Element('testsuites')
    totals = {'tests': 0, 'failures': 0, 'errors': 0, 'skipped': 0}
    by_suite = {}

    def suite_element(name):
        if name not in by_suite:
            by_suite[name] = ET.SubElement(suites, 'testsuite', name=name)
        return by_suite[name]

    for result in results:
        if result.junit and result.status in ('pass', 'fail'):
            try:
                document = ET.fromstring(result.junit)
            except ET.ParseError:
                document = None
            if document is not None:
                # doctest's JUnit reporter names classes after source files; use suites.
                suites_by_name = {case.name: case.suite for case in result.job.cases}
                for testcase in document.iter('testcase'):
                    suite = suites_by_name.get(testcase.get('name', ''), '') or testcase.get('classname') or 'tests'
                    testcase.set('classname', suite)
                    target = suite_element(suite)
                    target.append(testcase)
                    totals['tests'] += 1
                    totals['failures'] += len(testcase.findall('failure'))
                    totals['errors'] += len(testcase.findall('error'))
                continue
        for case in result.job.cases:
            testcase = ET.SubElement(suite_element(case.suite or 'tests'), 'testcase',
                                     classname=case.suite or 'tests', name=case.name, time=f'{result.seconds:.3f}')
            totals['tests'] += 1
            if result.status == 'skip':
                ET.SubElement(testcase, 'skipped', message='needs a display')
                totals['skipped'] += 1
            elif result.status != 'pass':
                error = ET.SubElement(testcase, 'error', message=result.status, type=result.status)
                error.text = result.output[-4000:]
                totals['errors'] += 1
    for key, value in totals.items():
        suites.set(key, str(value))
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(suites).write(path, encoding='unicode', xml_declaration=True)
    return totals


def report(results, skipped, args):
    failed = [result for result in results if result.status not in ('pass', 'skip')]
    for result in failed:
        print(f'\n===== {result.status.upper()} {result.job.label} =====', flush=True)
        lines = result.output.splitlines()
        print('\n'.join(lines[-args.tail:]) if len(lines) > args.tail else result.output, flush=True)
        if result.profile and args.keep_profiles:
            print(f'[run_tests] profile kept at {result.profile}', flush=True)
        for case in result.job.cases:
            if os.environ.get('GITHUB_ACTIONS') and case.file:
                print(f'::error file={case.file},line={case.line},title={case.label}::{result.status}', flush=True)
    passed = sum(1 for result in results if result.status == 'pass')
    total_seconds = sum(result.seconds for result in results)
    line = f'{passed} passed, {len(failed)} failed, {len(skipped)} skipped ({total_seconds:.1f}s of test time)'
    print('\n' + line, flush=True)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as out:
            out.write(f'### Native tests: {line}\n\n')
            if failed:
                out.write('| Result | Test | Seconds |\n| --- | --- | ---: |\n')
                for result in failed:
                    out.write(f'| {result.status} | `{result.job.label}` | {result.seconds:.1f} |\n')
                out.write('\n')
    return not failed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=None, help='native build directory (default: GLOB2_BUILD_DIR or build/<platform>/client/release)')
    parser.add_argument('--binary', choices=['engine', 'unit', 'all'], default='all')
    parser.add_argument('--list', action='store_true', help='print the selected cases and exit')
    parser.add_argument('--filter', action='append', default=[], metavar='GLOB', help='suite/name glob, repeatable')
    parser.add_argument('--tag', action='append', default=[], help='only cases with this tag, repeatable')
    parser.add_argument('--exclude-tag', action='append', default=[], help='skip cases with this tag, repeatable')
    parser.add_argument('--shard', metavar='K/N', help='run the K-th of N deterministic slices')
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 2)
    parser.add_argument('--display-jobs', type=int, default=None, help='parallelism for [display] cases (default: min(4, jobs))')
    parser.add_argument('--timeout', type=int, default=None, help='override every timeout, in seconds')
    parser.add_argument('--in-process', action='store_true', help='run the engine binary as one process')
    parser.add_argument('--no-display', action='store_true', help='skip [display] cases')
    parser.add_argument('--quick', action='store_true', help='skip [slow] cases')
    parser.add_argument('--update-fixtures', action='store_true', help='rewrite [golden] fixtures instead of checking them')
    parser.add_argument('--junit', type=Path, default=ROOT / 'artifacts' / 'tests' / 'junit.xml')
    parser.add_argument('--artifacts', type=Path, default=ROOT / 'artifacts' / 'tests')
    parser.add_argument('--keep-profiles', action='store_true', help='leave every disposable profile on disk')
    parser.add_argument('--tail', type=int, default=200, help='lines of output shown for a failure')
    parser.add_argument('--verbose', action='store_true', help='show successful assertions for in-process runs')
    args = parser.parse_args(argv)

    build_dir = (args.build_dir or native_build_directory()).resolve()
    kinds = ['engine', 'unit'] if args.binary == 'all' else [args.binary]
    cases = []
    for kind in kinds:
        binary = binary_path(build_dir, kind)
        if not binary.exists():
            if args.binary == 'all':
                print(f'[run_tests] {binary} is not built; skipping', flush=True)
                continue
            raise SystemExit(f'{binary} is not built; run: scons release=1 server=0 {kind}-tests')
        cases += list_cases(binary, kind)
    kept, skipped = select(cases, args)
    if args.list:
        for case in sorted(kept, key=lambda c: c.label):
            print(f'{case.label}' + (f'  [{"][".join(case.tags)}]' if case.tags else ''))
        print(f'{len(kept)} cases' + (f', {len(skipped)} need a display' if skipped else ''))
        return 0
    jobs = shard(make_jobs(kept, args), args.shard)
    if args.timeout:
        Job.timeout = property(lambda self, t=args.timeout: t)
    network_lock = threading.Lock()
    display_slots = threading.Semaphore(args.display_jobs or min(4, args.jobs))

    def run(job):
        if job.has('network'):
            with network_lock:
                return run_job(job, args, build_dir)
        if job.display:
            with display_slots:
                return run_job(job, args, build_dir)
        return run_job(job, args, build_dir)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {pool.submit(run, job): job for job in jobs}
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            mark = {'pass': 'PASS', 'fail': 'FAIL', 'error': 'ERROR', 'timeout': 'TIMEOUT'}[result.status]
            print(f'{mark} {result.job.label} ({result.seconds:.1f}s)', flush=True)
    results.sort(key=lambda result: result.job.label)
    skipped_results = [Result(Job(case.binary, [case]), 'skip', 0.0) for case in skipped]
    totals = merge_junit(results + skipped_results, args.junit)
    ok = report(results, skipped, args)
    print(f'[run_tests] JUnit report: {args.junit} ({totals["tests"]} cases)', flush=True)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
