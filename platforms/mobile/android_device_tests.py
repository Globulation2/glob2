#!/usr/bin/env python3
"""Run the cross-compiled doctest binaries on one explicitly selected Android device.

Uses a disposable /data/local/tmp directory, never the installed app's data. The
binaries run with SDL's dummy video and audio drivers and the software renderer,
so they cover CPU, input, persistence and software-rendering behaviour; they
complement, not replace, installed-APK renderer, keyboard and lifecycle checks.

Every case runs in its own process with a fresh profile and a timeout, as on the
desktop runner, in chunks of cases per device invocation. A hang or a crash costs
one case, and a chunk the device interrupted by rebooting is discarded and run
again. result.json, one JUnit file per binary and each case's log are written
after every chunk; --resume continues an interrupted run in the same --output.
"""
import argparse
import fnmatch
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test'))
sys.path.insert(0, str(ROOT / 'buildsys'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_provenance import source_identity, build_issues  # noqa: E402
import run_tests  # noqa: E402

BINARIES = {'glob2-unit-tests': 'unit', 'glob2-engine-tests': 'engine'}
# Repository files the cases read through glob2test::sourceRoot(). The packaged
# runtime tree is staged beside them as source/content.
SOURCE_FIXTURES = ('test/fixtures', 'test/maxima/fixtures', 'test/data', 'test/map-generator-resource-epoch.json',
                   'examples', 'libs/usl/test', 'services/online/packages/protocol/fixtures',
                   'tools/terrain_builtin_names.json', 'docs/multiplayer/lan.md',
                   'docs/multiplayer/lan-playtest.md', 'docs/development/mcp-ui-server.md')
# Saved games the cases load; test inputs, so the APK does not ship them.
CONTENT_FIXTURES = ('games',)
# CI runs the [map-generators] contract only in its dedicated Linux job.
DEFAULT_EXCLUDED_TAGS = ('map-generators',)
SEPARATOR = '\x1f'

# Runs one chunk, each case in its own process with a fresh profile and a timeout.
# Arguments: binary, case list (index, timeout, suite and name patterns separated
# by SEPARATOR), results directory.
DEVICE_SCRIPT = r'''#!/system/bin/sh
root=$(cd "$(dirname "$0")" && pwd)
binary=$1 list=$2 out=$3
mkdir -p "$out"
separator=$(printf '\037')
while IFS="$separator" read -r index limit suite pattern; do
  [ -n "$index" ] || continue
  profile=$root/profiles/$binary/$index
  rm -rf "$profile"
  mkdir -p "$profile"
  echo "$index" > "$out/current"
  if [ -n "$suite" ]; then set -- "-ts=$suite"; else set --; fi
  (cd "$root/source" && exec env LD_LIBRARY_PATH="$root/bin" SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
     SDL_AUDIODRIVER=dummy GLOB2_USER_DATA_DIR="$profile" GLOB2_ASSET_DIR="$root/source/content" \
     GLOB2_TEST_SOURCE_ROOT="$root/source" GLOB2_TEST_ARTIFACTS_ROOT="$root/artifacts" \
     timeout -k 5 "$limit" "$root/bin/$binary" "-tc=$pattern" "$@" -r=junit "-o=$out/$index.xml") \
     > "$out/$index.log" 2>&1 < /dev/null
  code=$?
  echo "$code" > "$out/$index.exit"
  [ "$code" = 0 ] && rm -rf "$profile"
done < "$list"
rm -f "$out/current"
echo done > "$out/finished"
'''


class Device:
    def __init__(self, sdk, port, serial):
        self.adb = [str(sdk / 'platform-tools/adb'), '-P', str(port), '-s', serial]

    def run(self, *arguments, timeout=120, check=True, attempts=3):
        """One adb command, retried across transient transport loss."""
        error = ''
        for attempt in range(attempts):
            try:
                result = subprocess.run(self.adb + list(arguments), capture_output=True, text=True,
                                        errors='replace', timeout=timeout)
                if result.returncode == 0 or not check:
                    return result
                error = result.stderr.strip() or result.stdout.strip()
            except subprocess.TimeoutExpired:
                error = 'timed out'
            if attempt + 1 < attempts:
                self.wait()
        raise RuntimeError(f'adb {" ".join(arguments[:2])} failed: {error}')

    def shell(self, command, timeout=120, check=True):
        return self.run('shell', command, timeout=timeout, check=check).stdout

    def wait(self, timeout=900):
        """Wait until the device is reachable and has finished booting."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                subprocess.run(self.adb + ['wait-for-device'], timeout=60, capture_output=True)
                booted = subprocess.run(self.adb + ['shell', 'getprop', 'sys.boot_completed'],
                                        capture_output=True, text=True, timeout=30).stdout.strip()
                if booted == '1':
                    return
            except subprocess.TimeoutExpired:
                pass
            time.sleep(5)
        raise RuntimeError(f'Device did not come back within {timeout} s')

    def boot_id(self):
        return self.shell('cat /proc/sys/kernel/random/boot_id').strip()


def retrieve(adb, remote, destination, errors, xml=False):
    """Keep failed transfers actionable and never accept an absent/old report."""
    result = subprocess.run(adb + ['pull', remote, str(destination)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        if result.returncode:
            raise ValueError(result.stdout.strip() or 'adb pull failed')
        if xml:
            ET.parse(destination)
        elif not destination.is_dir() or not any(destination.rglob('*')):
            raise ValueError('Empty artifact directory')
    except (OSError, ValueError, ET.ParseError) as error:
        errors.append({'remote': remote, 'error': str(error)})
        return False
    return True


def dependency_prefix(build):
    """The dependency prefix the test binaries were linked against."""
    configuration = (build / 'mobile-configuration.cmake').read_text()
    match = re.search(r'set\(GLOB2_MOBILE_PREFIX\s+"([^"]+)"', configuration)
    if not match:
        raise RuntimeError('Cannot find GLOB2_MOBILE_PREFIX in ' + str(build / 'mobile-configuration.cmake'))
    return Path(match.group(1))


def stage(args, names, payload, summary):
    """Binaries, shared libraries, the packaged runtime tree and the test fixtures."""
    ndk = json.loads((ROOT / 'platforms/mobile/toolchain.json').read_text())['android']['ndk']
    prebuilt = next((args.android_sdk / 'ndk' / ndk / 'toolchains/llvm/prebuilt').iterdir())
    summary['compilerVersion'] = subprocess.check_output([str(prebuilt / 'bin/clang++'), '--version'], text=True).strip()
    binaries = payload / 'bin'
    binaries.mkdir(parents=True)
    hashes = {}
    for name in names:
        subprocess.run([str(prebuilt / 'bin/llvm-strip'), '--strip-debug', '-o',
                        str(binaries / name), str(args.build / 'tests' / name)], check=True)
        (binaries / name).chmod(0o755)
        hashes[name] = hashlib.sha256((binaries / name).read_bytes()).hexdigest()
    prefix = args.mobile_deps or dependency_prefix(args.build)
    dependencies = json.loads((prefix / 'manifest.json').read_text())
    summary['dependencies'] = dependencies
    for filename in dependencies['archives']:
        if filename.endswith('.so'):
            shutil.copy2(prefix / filename, binaries / Path(filename).name)
    triple = {'arm64-v8a': 'aarch64-linux-android', 'armeabi-v7a': 'arm-linux-androideabi',
              'x86_64': 'x86_64-linux-android'}[args.arch]
    shutil.copy2(prebuilt / 'sysroot/usr/lib' / triple / 'libc++_shared.so', binaries / 'libc++_shared.so')
    (payload / 'run-cases.sh').write_text(DEVICE_SCRIPT)
    # The runtime tree the APK ships (WebP sprites, packed sheets, shaders), so
    # the cases load assets the way the installed game does.
    import android
    exported = android.export_packaged_assets(args.build, release=True, sprite_packing=args.sprite_packing,
                                              jobs=args.jobs)
    source = payload / 'source'
    shutil.copytree(exported, source / 'content')
    for name in CONTENT_FIXTURES:
        shutil.copytree(ROOT / 'content' / name, source / 'content' / name)
    for relative in SOURCE_FIXTURES:
        origin, target = ROOT / relative, source / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if origin.is_dir():
            shutil.copytree(origin, target, ignore=shutil.ignore_patterns('node_modules', '__pycache__'))
        else:
            shutil.copy2(origin, target)
    digest = hashlib.sha256()
    for path in sorted(payload.rglob('*')):
        if path.is_file():
            digest.update(path.relative_to(payload).as_posix().encode() + b'\0'
                          + hashlib.sha256(path.read_bytes()).digest())
    return hashes, digest.hexdigest()


def case_environment(remote, profile):
    return (f'env LD_LIBRARY_PATH={remote}/bin SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software '
            f'SDL_AUDIODRIVER=dummy GLOB2_USER_DATA_DIR={profile} GLOB2_ASSET_DIR={remote}/source/content '
            f'GLOB2_TEST_SOURCE_ROOT={remote}/source GLOB2_TEST_ARTIFACTS_ROOT={remote}/artifacts')


def list_cases(device, remote, name, kind, no_skip=False):
    """The binary's own listing on the device, with suites, files and tags."""
    profile = f'{remote}/profiles/listing'
    text = device.shell(f'mkdir -p {profile} && cd {remote}/source && {case_environment(remote, profile)} '
                        f'{remote}/bin/{name} -r=xml -ltc' + (' --no-skip' if no_skip else '') + ' 2>/dev/null',
                        timeout=300)
    start, end = text.find('<?xml'), text.rfind('</doctest>')
    if start < 0 or end < 0:
        raise RuntimeError(f'{name} could not list its test cases:\n{text[-2000:]}')
    return run_tests.parse_listing(text[start:end + len('</doctest>')], kind)


def listing_notes(device, remote, name):
    """What the binary reports about cases its decorators skip in this environment."""
    profile = f'{remote}/profiles/listing'
    text = device.shell(f'cd {remote}/source && {case_environment(remote, profile)} {remote}/bin/{name} -ltc '
                        '2>&1 >/dev/null', timeout=300, check=False)
    return sorted({line.strip() for line in text.splitlines() if line.startswith('glob2test:')})


def case_key(case):
    return f'{case.file}:{case.line}:{case.suite}/{case.name}'


def executed_cases(junit):
    try:
        return [case for case in ET.fromstring(junit).iter('testcase') if case.find('skipped') is None]
    except ET.ParseError:
        return []


def outcome(code, junit):
    """pass, fail, timeout or crash, from one case's exit code and JUnit report."""
    executed = executed_cases(junit) if junit else []
    if code in (124, 137):
        return 'timeout'
    if code is None or code >= 128 or (code and not executed):
        return 'crash'
    if code or not executed:
        return 'fail'
    if any(case.find('failure') is not None or case.find('error') is not None for case in executed):
        return 'fail'
    return 'pass'


def failure_details(junit, log):
    """A one-line message and the text a reviewer needs, from the report or else the log."""
    for testcase in executed_cases(junit) if junit else []:
        for element in list(testcase.iter('failure')) + list(testcase.iter('error')):
            text = (element.get('message', '') + '\n' + (element.text or '')).strip()
            return element.get('message', 'failed')[:500], text[-8000:]
    return 'no JUnit report', log[-8000:]


def write_reports(output, summary, state):
    for name, record in state['binaries'].items():
        suites = ET.Element('testsuites')
        totals = {'tests': 0, 'failures': 0, 'errors': 0, 'skipped': 0}
        by_suite = {}
        for key in record.get('order', ()):
            result = record['results'].get(key)
            case = record['cases'][key]
            suite = case['suite'] or case['file'] or 'tests'
            if suite not in by_suite:
                by_suite[suite] = ET.SubElement(suites, 'testsuite', name=suite)
            testcase = ET.SubElement(by_suite[suite], 'testcase', classname=suite, name=case['name'],
                                     file=case['file'], line=str(case['line']),
                                     time=f"{(result or {}).get('seconds', 0):.3f}")
            totals['tests'] += 1
            if result is None:
                ET.SubElement(testcase, 'skipped', message='not run yet')
                totals['skipped'] += 1
            elif result['status'] == 'skip':
                ET.SubElement(testcase, 'skipped', message=result.get('reason', 'skipped'))
                totals['skipped'] += 1
            elif result['status'] == 'fail':
                failure = ET.SubElement(testcase, 'failure', message=result.get('message', 'failed'), type='fail')
                failure.text = result.get('details', '')
                totals['failures'] += 1
            elif result['status'] != 'pass':
                error = ET.SubElement(testcase, 'error', message=result.get('message', result['status']),
                                      type=result['status'])
                error.text = result.get('details', '')
                totals['errors'] += 1
        for key, value in totals.items():
            suites.set(key, str(value))
        ET.ElementTree(suites).write(output / (name + '.xml'), encoding='unicode', xml_declaration=True)
        counts = {'pass': 0, 'fail': 0, 'timeout': 0, 'crash': 0, 'skip': 0}
        for result in record['results'].values():
            counts[result['status']] = counts.get(result['status'], 0) + 1
        counts['notRun'] = len(record.get('order', ())) - len(record['results'])
        record['counts'] = counts
    summary['binaries'] = {name: {'counts': record['counts'], 'reboots': record.get('reboots', 0),
                                  'junit': name + '.xml'}
                           for name, record in state['binaries'].items()}
    (output / 'state.json').write_text(json.dumps(state, indent=1) + '\n')
    (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')


def run_chunk(device, args, remote, name, record, keys, chunk, output):
    """Run KEYS on the device and return their results, or None if the device rebooted."""
    lines = []
    for index, key in enumerate(keys):
        case = record['cases'][key]
        lines.append(SEPARATOR.join((str(index), str(case['timeout']), run_tests.doctest_pattern(case['suite']),
                                     run_tests.doctest_pattern(case['name']))))
    results_dir = f'{remote}/results/{name}/{chunk}'
    with tempfile.TemporaryDirectory(prefix='glob2-device-chunk-') as directory:
        listing = Path(directory) / 'cases.list'
        listing.write_text('\n'.join(lines) + '\n')
        device.shell(f'rm -rf {results_dir} && mkdir -p {results_dir}')
        device.run('push', str(listing), results_dir + '.list')
    boot = device.boot_id()
    # Detached, so the chunk survives a dropped adb connection.
    device.shell(f'(nohup sh {remote}/run-cases.sh {name} {results_dir}.list {results_dir} > /dev/null 2>&1 '
                 '< /dev/null &) > /dev/null 2>&1 < /dev/null', timeout=30)
    current, since, watchdog = None, time.monotonic(), 120
    while True:
        time.sleep(args.poll)
        try:
            if device.boot_id() != boot:
                device.wait()
                return None
            status = device.shell(f'cat {results_dir}/current 2>/dev/null; echo; '
                                  f'test -e {results_dir}/finished && echo FINISHED', check=False)
        except RuntimeError:
            device.wait()
            if device.boot_id() != boot:
                return None
            continue
        if 'FINISHED' in status:
            break
        running = status.split('\n', 1)[0].strip()
        if running != current:
            current, since = running, time.monotonic()
            watchdog = int(record['cases'][keys[int(running)]]['timeout']) + 60 if running.isdigit() else 120
        elif time.monotonic() - since > watchdog:
            # The device-side timeout should already have stopped this case.
            device.shell(f'pid=$(pidof {name}); [ -n "$pid" ] && kill -9 $pid', check=False)
            since = time.monotonic()
    local = output / 'logs' / name / str(chunk)
    if local.exists():
        shutil.rmtree(local)
    local.parent.mkdir(parents=True, exist_ok=True)
    device.run('pull', results_dir, str(local), timeout=600)
    results = {}
    for index, key in enumerate(keys):
        code_file, junit_file, log_file = local / f'{index}.exit', local / f'{index}.xml', local / f'{index}.log'
        code = int(code_file.read_text().strip()) if code_file.is_file() else None
        junit = junit_file.read_text(errors='replace') if junit_file.is_file() else ''
        log = log_file.read_text(errors='replace') if log_file.is_file() else ''
        status = outcome(code, junit)
        seconds = sum(float(case.get('time', 0) or 0) for case in executed_cases(junit)) if junit else 0.0
        result = {'status': status, 'exitCode': code, 'seconds': seconds,
                  'log': log_file.relative_to(output).as_posix()}
        if status != 'pass':
            result['message'], result['details'] = failure_details(junit, log)
            if status == 'timeout':
                result['message'] = f"exceeded {record['cases'][key]['timeout']} s"
        proofs = [line.partition('GLOB2_TEST_PROVENANCE ')[2] for line in log.splitlines()
                  if line.startswith('GLOB2_TEST_PROVENANCE ')]
        if proofs and 'provenance' not in record:
            record['provenance'] = json.loads(proofs[0])
        results[key] = result
    return results


def select_cases(device, args, remote, name, record):
    kind = BINARIES[name]
    listed = list_cases(device, remote, name, kind)
    everything = list_cases(device, remote, name, kind, no_skip=True)
    selection = argparse.Namespace(filter=args.filter, tag=args.tag, exclude_tag=args.exclude_tag,
                                   tier='full' if args.slow else 'without-slow', no_display=True)
    kept, display = run_tests.select(listed, selection)
    listed_keys = {case_key(case) for case in listed}
    decorated, _ = run_tests.select([case for case in everything if case_key(case) not in listed_keys], selection)
    if args.suite:
        kept, display, decorated = ([case for case in group if fnmatch.fnmatchcase(case.suite, args.suite)]
                                    for group in (kept, display, decorated))
    timings = run_tests.reference_timings()
    record['cases'], record['order'] = {}, []
    for case in kept + display + decorated:
        key = case_key(case)
        if args.case_timeout:
            limit = args.case_timeout
        elif case.label in timings:
            limit = run_tests.case_timeout(case.label, timings) * args.timeout_scale
        else:
            limit = run_tests.UNKNOWN_TIMEOUT
        record['cases'][key] = {'suite': case.suite, 'name': case.name, 'file': case.file, 'line': case.line,
                                'timeout': limit}
        record['order'].append(key)
    for case in display:
        record['results'][case_key(case)] = {'status': 'skip',
                                             'reason': 'needs a real window; the device runner uses dummy video'}
    reason = '; '.join(listing_notes(device, remote, name)) or 'skipped by its decorator in this environment'
    for case in decorated:
        record['results'][case_key(case)] = {'status': 'skip', 'reason': reason}


def run_binary(device, args, remote, name, state, summary, output):
    record = state['binaries'].setdefault(name, {'results': {}, 'reboots': 0})
    if 'order' not in record:
        select_cases(device, args, remote, name, record)
        write_reports(output, summary, state)
    pending = [key for key in record['order'] if key not in record['results']]
    print(f'{name}: {len(record["order"])} cases selected, {len(pending)} to run', flush=True)
    interrupted = {}
    while pending:
        # After a reboot, run the interrupted cases one at a time to find the culprit.
        size = 1 if interrupted.get(pending[0]) else args.chunk
        keys = pending[:size]
        chunk = state['chunk'] = state.get('chunk', 0) + 1
        results = run_chunk(device, args, remote, name, record, keys, chunk, output)
        if results is None:
            record['reboots'] = record.get('reboots', 0) + 1
            for key in keys:
                interrupted[key] = interrupted.get(key, 0) + 1
            if size == 1 and interrupted[keys[0]] > 2:
                record['results'][keys[0]] = {'status': 'crash', 'message': 'the device rebooted while it ran',
                                              'details': 'the device rebooted twice while this case ran alone'}
            print(f'{name}: chunk {chunk} discarded; the device rebooted', flush=True)
        else:
            record['results'].update(results)
            bad = [f"{record['cases'][key]['name']} ({results[key]['status']})"
                   for key in keys if results[key]['status'] != 'pass']
            print(f'{name}: chunk {chunk}: {len(keys) - len(bad)}/{len(keys)} passed'
                  + (''.join('\n  ' + entry for entry in bad)), flush=True)
        write_reports(output, summary, state)
        pending = [key for key in record['order'] if key not in record['results']]
    write_reports(output, summary, state)
    # One entry per binary, as the evidence checkers read it: exitCode 0 when every
    # selected case passed or was skipped with a reason.
    counts = record['counts']
    build = record.get('provenance', {})
    entry = {'name': name, 'exitCode': int(any(counts[status] for status in ('fail', 'timeout', 'crash', 'notRun'))),
             'counts': counts, 'build': build, 'provenanceIssues': build_issues(build, summary),
             'binarySha256': hashlib.sha256((args.build / 'tests' / name).read_bytes()).hexdigest(),
             'executedBinarySha256': state['executedHashes'][name]}
    summary['tests'] = [test for test in summary.get('tests', []) if test['name'] != name] + [entry]
    write_reports(output, summary, state)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--adb-port', type=int, default=5037, help='Explicit ADB server port for an isolated emulator')
    parser.add_argument('--android-sdk', type=Path, help='Android SDK (default: the managed development SDK)')
    parser.add_argument('--arch', choices=('arm64-v8a', 'armeabi-v7a', 'x86_64'), default='arm64-v8a')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/android/device-tests')
    parser.add_argument('--resume', action='store_true', help='Continue the run recorded in --output')
    parser.add_argument('--binary', action='append', choices=tuple(BINARIES))
    parser.add_argument('--mobile-deps', type=Path, help='Dependency prefix (default: the one the binaries linked)')
    parser.add_argument('--suite', help='Only suites matching this glob, e.g. JavaScript*')
    parser.add_argument('--filter', action='append', default=[], help='SUITE/CASE glob, as for run_tests.py')
    parser.add_argument('--tag', action='append', default=[], help='Only cases with this tag')
    parser.add_argument('--exclude-tag', action='append', default=list(DEFAULT_EXCLUDED_TAGS),
                        help='Leave out cases with this tag, in addition to map-generators')
    parser.add_argument('--slow', action='store_true', help='Include [slow] cases')
    parser.add_argument('--chunk', type=int, default=20, help='Cases per device invocation (default 20)')
    parser.add_argument('--timeout-scale', type=int, default=4,
                        help="Multiplier on the desktop runner's recorded-duration timeouts (default 4)")
    parser.add_argument('--case-timeout', type=int, help='One per-case timeout in seconds for every case')
    parser.add_argument('--poll', type=float, default=3.0, help=argparse.SUPPRESS)
    parser.add_argument('--sprite-packing', choices=('auto', 'legacy', 'webp', 'video'), default='auto')
    parser.add_argument('--jobs', type=int, default=8, help='Asset export workers')
    parser.add_argument('--keep-remote', action='store_true')
    args = parser.parse_args()
    if args.android_sdk is None:
        from dev_paths import android_sdk
        args.android_sdk = Path(android_sdk(ROOT))
    if args.tag:
        args.exclude_tag = [tag for tag in args.exclude_tag if tag not in args.tag]
    device = Device(args.android_sdk, args.adb_port, args.serial)
    if device.run('get-state').stdout.strip() != 'device':
        raise RuntimeError('Selected device is not authorized')
    if args.arch not in device.shell('getprop ro.product.cpu.abilist').strip().split(','):
        raise RuntimeError('Device does not support ' + args.arch)
    args.build = ROOT / f'build/android/device/{args.arch}/24/client/release'
    names = args.binary or tuple(BINARIES)
    for name in names:
        if not (args.build / 'tests' / name).is_file():
            raise RuntimeError('Build android-tests before running: ' + name)
    output = args.output.resolve()
    if args.resume:
        state = json.loads((output / 'state.json').read_text())
        summary = json.loads((output / 'result.json').read_text())
    else:
        output.mkdir(parents=True, exist_ok=False)
        state = {'binaries': {}}
        summary = {'serial': args.serial, 'arch': args.arch, 'tests': [],
                   'mode': 'native Android CPU, SDL dummy video and audio, software renderer; one process per case',
                   'suite': args.suite or '*', 'excludedTags': args.exclude_tag, 'retrievalErrors': [],
                   'remoteRetained': True}
        summary.update(source_identity())
        summary['buildConfiguration'] = {name: (args.build / name).read_text()
                                         for name in ('identity.json', 'toolchain.json', 'options.json')
                                         if (args.build / name).is_file()}
        summary['device'] = {key: device.shell('getprop ' + key).strip()
                             for key in ('ro.product.model', 'ro.build.version.release', 'ro.build.version.sdk',
                                         'ro.product.cpu.abilist')}
    remote = state.get('remote')
    staged = remote and device.shell(f'cat {remote}/staged 2>/dev/null', check=False).strip() == state['stageDigest']
    if not staged:
        if state['binaries']:
            raise RuntimeError('The staged files of the run to resume are gone from the device; start a new run')
        remote = state['remote'] = summary['remoteDirectory'] = '/data/local/tmp/glob2-tests-' + uuid.uuid4().hex
        with tempfile.TemporaryDirectory(prefix='glob2-device-tests-') as directory:
            payload = Path(directory)
            state['executedHashes'], state['stageDigest'] = stage(args, names, payload, summary)
            device.run('push', str(payload), remote, timeout=3600)
        device.shell(f'chmod 755 {remote}/bin/* && echo {state["stageDigest"]} > {remote}/staged')
    write_reports(output, summary, state)
    for name in names:
        run_binary(device, args, remote, name, state, summary, output)
    # Saves, traces and corpus files the cases left for review, including failures'.
    corpus = output / 'corpus'
    if corpus.exists():
        shutil.rmtree(corpus)
    retrieve(device.adb, remote + '/artifacts', corpus, summary['retrievalErrors'])
    counts = {name: state['binaries'][name]['counts'] for name in names}
    failed = (any(test['exitCode'] or test['provenanceIssues'] for test in summary['tests'])
              or summary['retrievalErrors'])
    if not args.keep_remote and not failed:
        device.shell('rm -rf ' + shlex.quote(remote))
        summary['remoteRetained'] = False
    write_reports(output, summary, state)
    for name in names:
        print(f'{name}: ' + ', '.join(f'{status} {count}' for status, count in counts[name].items()), flush=True)
    print('Results:', output)
    if failed:
        raise SystemExit(1)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
