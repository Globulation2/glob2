#!/usr/bin/env python3
"""Run the native doctest binaries: glob2-engine-tests and glob2-unit-tests.

Every engine test case runs in its own process with a disposable profile, dummy SDL
drivers unless it is tagged [display], a timeout and captured output shown only on
failure. Headless unit cases share a process; display unit cases run separately.
Results are merged into one JUnit file
and, under GitHub Actions, into the step summary with per-failure annotations.
Fullscreen checks within display cases run only with --fullscreen.

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
from collections import Counter
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
    subset: bool = False   # a whole-binary job that must run only its listed cases
    without_display: bool = False  # headless unit group excludes separately scheduled display cases
    without_benchmarks: bool = False  # a whole-binary job that skips only the [benchmark] cases

    @property
    def label(self):
        if not self.whole:
            return self.cases[0].label
        suite = f' {self.cases[0].suite}' if self.subset and self.cases[0].suite else ''
        return f'{BINARIES[self.binary]}{suite} ({len(self.cases)} cases)'

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
    note: str = ''       # a failure the runner found that the JUnit document does not record


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
        if case.has('benchmark') and 'benchmark' not in args.tag:
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


def make_jobs(cases, args, all_cases=None):
    """all_cases: the full listing, so a whole-binary job knows when a filter applied."""
    jobs = []
    for kind in ('unit', 'engine'):
        mine = [case for case in cases if case.binary == kind]
        if not mine:
            continue
        if kind == 'unit':
            # Headless cases may change SDL's video driver. Display tests need
            # isolated processes, even in the otherwise in-process unit suite.
            jobs += [Job(kind, [case]) for case in mine if case.display]
            mine = [case for case in mine if not case.display]
            if not mine:
                continue
        if kind == 'unit' or args.in_process:
            everything = [case for case in (all_cases or [])
                          if case.binary == kind and (kind != 'unit' or not case.display)]
            left_out = [case for case in everything if case not in mine]
            if left_out and all(case.has('benchmark') for case in left_out):
                jobs.append(Job(kind, mine, whole=True, without_benchmarks=True, without_display=(kind == 'unit')))
            elif all_cases and len(mine) < len(everything):
                # doctest selects by name and by suite separately, so a filtered run is
                # one process per suite: -ts= keeps same-named cases of other suites out.
                by_suite = {}
                for case in mine:
                    by_suite.setdefault(case.suite, []).append(case)
                jobs += [Job(kind, group, whole=True, subset=True) for group in by_suite.values()]
            else:
                jobs.append(Job(kind, mine, whole=True, without_display=(kind == 'unit')))
        else:
            jobs += [Job(kind, [case]) for case in mine]
    return jobs


def doctest_pattern(name):
    """Quote separators in doctest's comma-separated filter grammar."""
    return name.replace('\\', '\\\\').replace(',', '\\,')


def doctest_filter(job):
    if job.whole:
        if job.without_benchmarks:
            excluded = '*[benchmark]*' + (',*[display*' if job.without_display else '')
            return ['-tce=' + excluded]
        if not job.subset:
            return ['-tce=*[display*'] if job.without_display else []
        filters = ['-tc=' + ','.join(doctest_pattern(case.name) for case in job.cases)]
        if job.cases[0].suite:
            filters.append('-ts=' + doctest_pattern(job.cases[0].suite))
        return filters
    case = job.cases[0]
    filters = ['-tc=' + doctest_pattern(case.name)]
    if case.suite:
        filters.append('-ts=' + doctest_pattern(case.suite))
    return filters


def junit_execution_issue(job, junit_text):
    """A successful exit must prove that every selected case actually executed."""
    if not junit_text:
        return 'test process produced no JUnit report'
    try:
        document = ET.fromstring(junit_text)
    except ET.ParseError as error:
        return f'invalid JUnit report: {error}'
    executed = [case for case in document.iter('testcase') if case.find('skipped') is None]
    if not executed:
        return 'JUnit report contains no executed test cases'
    expected = Counter((case.file, case.name) for case in job.cases)
    actual = Counter((case.get('classname', ''), case.get('name', '')) for case in executed)
    missing, unexpected = expected - actual, actual - expected
    if missing or unexpected:
        def describe(cases):
            return '; '.join(f'{file}/{name} ({count})' for (file, name), count in cases.items())

        details = []
        if missing:
            details.append('missing selected cases: ' + describe(missing))
        if unexpected:
            details.append('unexpected executed cases: ' + describe(unexpected))
        return 'JUnit execution does not match selection: ' + '; '.join(details)
    if any(case.find('failure') is not None or case.find('error') is not None for case in executed):
        return 'JUnit records test failures despite a successful process exit'
    return ''


def xvfb_prefix(job):
    if not job.display or platform.system() != 'Linux' or os.environ.get('DISPLAY'):
        return []
    xvfb = shutil.which('xvfb-run')
    if not xvfb:
        return []
    # SDL closes its last X connection between contexts. Keep Xvfb from
    # resetting while the next context reconnects.
    return [xvfb, '-a', '-s', f'-screen 0 {job.screen}x24 -noreset',
            sys.executable, str(ROOT / 'test' / 'xvfb_session.py')]


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
    # Always override inherited opt-in: a standard run must not take over the desktop.
    env['GLOB2_TEST_FULLSCREEN'] = '1' if args.fullscreen else '0'
    if job.display:
        env['GLOB2_TEST_DISPLAY'] = '1'
        env.pop('SDL_VIDEODRIVER', None)
        if platform.system() == 'Linux':
            env.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
    else:
        env['SDL_VIDEODRIVER'] = 'dummy'
        env['SDL_RENDER_DRIVER'] = 'software'  # the dummy driver has no accelerated renderer
    if args.update_fixtures:
        env['GLOB2_TEST_UPDATE_FIXTURES'] = '1'
    if args.artifacts:
        artifacts = Path(args.artifacts).resolve()
        if job.whole:
            # Every case in the process derives <root>/<suite>/<case> itself.
            env['GLOB2_TEST_ARTIFACTS_ROOT'] = str(artifacts)
            env.pop('GLOB2_TEST_ARTIFACTS', None)
        else:
            case = job.cases[0]
            env['GLOB2_TEST_ARTIFACTS'] = str(artifacts / sanitized(case.suite or 'no-suite') / sanitized(case.name))
    command = xvfb_prefix(job) + [str(binary), '-r=junit', f'-o={junit}', '--no-breaks=true'] + doctest_filter(job)
    if args.verbose and job.whole:
        command += ['-s']
    started = time.monotonic()
    status, output = 'pass', ''
    diagnostics = ''
    with open(root / 'output.txt', 'w+', encoding='utf-8', errors='replace') as capture:
        popen_kwargs = dict(cwd=work, env=env, stdout=capture, stderr=subprocess.STDOUT)
        if os.name != 'nt':
            popen_kwargs['start_new_session'] = True
        process = subprocess.Popen(command, **popen_kwargs)
        try:
            code = process.wait(timeout=job.timeout)
        except subprocess.TimeoutExpired:
            diagnostics = timeout_diagnostics(process.pid, binary)
            terminate(process)
            code = None
        capture.seek(0)
        output = capture.read() + diagnostics
    seconds = time.monotonic() - started
    if code is None:
        status = 'timeout'
        output += f'\n[run_tests] killed after {job.timeout}s\n'
    elif code != 0:
        status = 'fail' if junit.exists() and code == 1 else 'error'
        if status == 'error':
            output += f'\n[run_tests] exit status {code}\n'
    note = ''
    if status == 'pass' and not job.has('writes-preferences') and not job.whole:
        after = (preferences.read_bytes(), preferences.stat().st_mtime_ns) if preferences.exists() else None
        if after != before:
            status = 'fail'
            note = 'the test changed the profile preferences; tag it [writes-preferences] if that is intended'
            output += f'\n[run_tests] {note}\n'
    junit_text = junit.read_text(encoding='utf-8', errors='replace') if junit.exists() else ''
    if status == 'pass':
        issue = junit_execution_issue(job, junit_text)
        if issue:
            status = 'error'
            note = issue
            output += f'\n[run_tests] {issue}\n'
    if status != 'pass' and junit_text:
        output += failure_details(junit_text)
    result = Result(job, status, seconds, output, junit_text, str(root), note)
    if not keep:
        shutil.rmtree(root, ignore_errors=True)
    return result


def failure_details(junit_text):
    """The doctest JUnit reporter is the only reporter running, so surface its messages."""
    try:
        document = ET.fromstring(junit_text)
    except ET.ParseError:
        return ''
    lines = []
    for testcase in document.iter('testcase'):
        for element in list(testcase.findall('failure')) + list(testcase.findall('error')):
            lines.append(f"[{element.tag}] {testcase.get('name', '')}: {element.get('message', '')}")
            if element.text and element.text.strip():
                lines.append(element.text.strip())
    return ('\n' + '\n'.join(lines) + '\n') if lines else ''



def timeout_display_state(pid, owned_displays):
    """Capture only an Xvfb display owned by the timed-out test group."""
    lines = ['[run_tests] owned Xvfb window state:']
    try:
        environment = dict(entry.split('=', 1) for entry in
                           (Path('/proc') / str(pid) / 'environ').read_bytes().decode().split('\0')
                           if '=' in entry)
        if environment.get('DISPLAY') not in owned_displays:
            return ''
        deadline = time.monotonic() + 6
        commands = [['xprop', '-root', '_NET_SUPPORTING_WM_CHECK', '_NET_CLIENT_LIST'],
                    ['xwininfo', '-root', '-tree']]
        while commands and time.monotonic() < deadline:
            command = commands.pop(0)
            if not shutil.which(command[0]):
                continue
            state = subprocess.run(command, env=environment, capture_output=True, text=True,
                                   timeout=min(2, max(0.1, deadline - time.monotonic())))
            lines.extend([' '.join(command), state.stdout, state.stderr])
            if command == ['xwininfo', '-root', '-tree']:
                windows = re.findall(r'^\s*(0x[0-9a-fA-F]+)\s+"', state.stdout, re.MULTILINE)
                commands.extend(['xwininfo', '-id', window, '-all'] for window in windows[:8])
        return '\n'.join(lines)
    except (OSError, UnicodeError, subprocess.SubprocessError) as error:
        lines.append(f'[run_tests] Xvfb diagnostics unavailable: {error}')
        return '\n'.join(lines)


def timeout_diagnostics(group, binary):
    """Inspect only this test's owned Linux process group before timeout cleanup."""
    if platform.system() != 'Linux':
        return ''
    lines = ['\n[run_tests] timeout process group:']
    try:
        snapshot = subprocess.run(['ps', '-eo', 'pid=,ppid=,pgid=,stat=,args='],
                                  capture_output=True, text=True, timeout=2, check=True)
        children = []
        displays = set()
        for row in snapshot.stdout.splitlines():
            fields = row.split(None, 4)
            if len(fields) != 5 or fields[2] != str(group):
                continue
            lines.append(row)
            arguments = fields[4].split()
            if len(arguments) > 1 and Path(arguments[0]).name == 'Xvfb':
                displays.add(arguments[1])
            pid = int(fields[0])
            process_path = Path('/proc') / str(pid)
            try:
                for task in sorted((process_path / 'task').iterdir()):
                    lines.append(f'  thread {task.name}: {(task / "wchan").read_text().strip()}')
                if (process_path / 'exe').resolve() == binary.resolve():
                    children.append(pid)
            except OSError as error:
                lines.append(f'  /proc/{pid}: {error}')
        if children and displays:
            lines.append(timeout_display_state(children[0], displays))
        # GitHub's ptrace policy needs sudo for sibling processes. Only attach to
        # the actual test executable in the owned group, never its X server/WM.
        debugger = shutil.which('gdb')
        if os.environ.get('GITHUB_ACTIONS') == 'true' and debugger and children:
            command = ['sudo', '-n', debugger, '--nx', '--batch', '--quiet', '--iex', 'set auto-load off', '--pid', str(children[0]),
                       '--ex', 'set pagination off', '--ex', 'thread apply all bt', '--ex', 'detach']
            trace = subprocess.run(command, capture_output=True, text=True, timeout=10)
            lines.extend([trace.stdout, trace.stderr])
    except (OSError, subprocess.SubprocessError) as error:
        lines.append(f'[run_tests] timeout diagnostics unavailable: {error}')
    return '\n'.join(lines) + '\n'


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
                # Names repeat across suites, so key by file and name and never guess.
                by_file_and_name = {(case.file, case.name): case.suite for case in result.job.cases}
                for testcase in document.iter('testcase'):
                    name = testcase.get('name', '')
                    suite = (by_file_and_name.get((testcase.get('classname', ''), name))
                             or testcase.get('classname') or 'tests')
                    testcase.set('classname', suite)
                    if result.note and not testcase.findall('failure') and not testcase.findall('error'):
                        failure = ET.SubElement(testcase, 'failure', message='run_tests: ' + result.note, type='run_tests')
                        failure.text = result.output[-4000:]
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
    parser.add_argument('--fullscreen', action='store_true', help='enable fullscreen checks within [display] cases (takes over the desktop; prefer a virtual display)')
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
            print(case.label)
        print(f'{len(kept)} cases' + (f', {len(skipped)} need a display' if skipped else ''))
        return 0
    jobs = shard(make_jobs(kept, args, cases), args.shard)
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
