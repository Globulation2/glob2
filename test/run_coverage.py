#!/usr/bin/env python3
"""Build/run native tests with Clang coverage, keeping each binary's profiles separate.

Reports are evidence, not a repository-wide or cross-platform coverage claim.
The engine report is the implementation baseline; unit results are supplemental.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import shutil
import shlex
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.github/scripts'))
from ci_policy import is_test_source  # tests sit beside the code they test
IGNORE = r'/test/|/third_party/|/build/|/src/net/|/src/yog/|^/usr/|^/opt/|^/Applications/|^/Library/'


def tool(name):
    if platform.system() == 'Darwin':
        return ['xcrun', name]
    found = shutil.which(name)
    if found:
        return [found]
    raise RuntimeError(f'{name} is required (install the LLVM tools matching Clang)')


def run_logged(command, log, *, cwd=ROOT, env=None, stream_logs=False):
    """Keep the file log and optionally expose progress before artifact upload."""
    started = time.monotonic()
    if stream_logs:
        print(f'Coverage command ({log}): {shlex.join(command)}', flush=True)
    with log.open('w') as stream:
        if stream_logs:
            child_env = dict(os.environ if env is None else env)
            child_env['PYTHONUNBUFFERED'] = '1'
            with subprocess.Popen(command, cwd=cwd, env=child_env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, errors='replace') as child:
                for line in child.stdout:
                    stream.write(line)
                    stream.flush()
                    print(line, end='', flush=True)
                status = child.wait()
        else:
            status = subprocess.run(command, cwd=cwd, env=env, stdout=stream,
                                    stderr=subprocess.STDOUT).returncode
    if stream_logs:
        print(f'Coverage command finished: exit={status}, elapsed={time.monotonic()-started:.1f}s, log={log}', flush=True)
    return status


def compact_profiles(profiles, successful):
    """Drop redundant inputs only after passing tests and complete report generation."""
    if not successful:
        return {'raw_profiles_retained': True}
    size = sum(path.stat().st_size for path in profiles)
    for path in profiles:
        path.unlink()
    return {'raw_profiles_retained': False, 'raw_profile_bytes_removed': size}


def binary_label(binary, kind):
    """The test-runner label of a binary: glob2-map-tests -> map, src/glob2 -> client."""
    if kind == 'client':
        return 'client'
    return binary.name.removeprefix('glob2-').removesuffix('.exe').removesuffix('-tests')


def profiles_for_binary(profiles, binary, primary, profdata_tool):
    """The profiles one binary wrote.

    Per-job profiles name their writer's build ID (job-NNNNN.<id>.profdata, see
    test/case_coverage.py); raw profiles record it. Anything unattributable
    goes to the kind's primary binary.
    """
    import case_coverage
    wanted = case_coverage.binary_id_of_file(binary)
    kept = []
    for profile in profiles:
        if profile.suffix == '.profdata':
            _, _, identity = profile.stem.partition('.')
            if (wanted and identity == wanted) or (primary and (not identity or not wanted)):
                kept.append(profile)
            continue
        writer = case_coverage.binary_id(profile, profdata_tool[-1])
        if (wanted and writer == wanted) or (primary and (not writer or not wanted)):
            kept.append(profile)
    return kept


# test/run_tests.py --binary unit also runs glob2-map-tests, and --binary engine
# also runs glob2-sim-tests; their profiles land in the same directory.
KIND_COMPANIONS = {'unit': ('glob2-map-tests',), 'engine': ('glob2-sim-tests',)}


def kind_binaries(build, kind):
    """The binaries whose profiles a coverage kind collects, primary first."""
    if kind == 'client':
        return [build / 'src/glob2']
    test = build / 'test'
    return [test / f'glob2-{kind}-tests'] + [test / name for name in KIND_COMPANIONS.get(kind, ())
                                            if (test / name).exists()]


def instrumentation_flags(optimization, debug_info):
    debug = '-g' if debug_info == 'full' else '-gline-tables-only'
    return f'{debug} -O{optimization} -fprofile-instr-generate -fcoverage-mapping -DGLOB2_TEST_COVERAGE'


def profile_pattern(pool_size):
    # LLVM locks and merges the pool at process exit. Keep each binary's pool
    # in its own raw directory, just like the individual process profiles.
    return f'%{pool_size}m.profraw' if pool_size else '%m-%p.profraw'


COVERED_ROOTS = ('src/', 'libs/gag/', 'libs/usl/', 'third_party/natsort/', 'platforms/mobile/')


def area(path):
    parts = Path(path).parts
    if parts[0] == 'src' and len(parts) > 2:
        return '/'.join(parts[:3] if parts[1] in ('ai', 'map') and len(parts) > 3 else parts[:2])
    if parts[0] in ('libs', 'third_party') and len(parts) > 2:
        return '/'.join(parts[:2])
    return parts[0]


def summarize(export, root=ROOT):
    """Retain raw covered/count values; never average percentages or combine binaries."""
    files, areas = [], {}
    for item in export['data'][0]['files']:
        try:
            relative = Path(item['filename']).resolve().relative_to(root.resolve())
        except ValueError:
            continue
        if not relative.as_posix().startswith(COVERED_ROOTS):
            continue
        if str(relative).startswith(('src/net/', 'src/yog/')) or is_test_source(relative.as_posix()):
            continue
        row = {'path': relative.as_posix(), 'summary': item['summary']}
        files.append(row)
        if relative.suffix not in ('.c', '.cpp', '.cc', '.cxx'):
            continue
        group = areas.setdefault(area(relative), {})
        for metric in ('lines', 'branches', 'functions'):
            counts = group.setdefault(metric, {'count': 0, 'covered': 0})
            for key in counts:
                counts[key] += item['summary'][metric][key]
    for group in areas.values():
        for counts in group.values():
            counts['percent'] = 100 * counts['covered'] / counts['count'] if counts['count'] else None
    measured = {row['path'] for row in files}
    unmeasured = sorted(p.relative_to(root).as_posix()
                        for directory in COVERED_ROOTS
                        for p in (root / directory).rglob('*')
                        if p.suffix in ('.c', '.cpp', '.cc', '.cxx')
                        and p.relative_to(root).as_posix() not in measured
                        and not p.relative_to(root).as_posix().startswith(('src/net/', 'src/yog/'))
                        and not is_test_source(p.relative_to(root).as_posix()))
    uncovered_functions = []
    for function in export['data'][0].get('functions', []):
        if function['count'] or not function.get('regions'):
            continue
        region = function['regions'][0]
        definition = function['filenames'][region[5]]
        try:
            path = Path(definition).resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            continue
        if path not in measured or Path(path).suffix not in ('.c', '.cpp', '.cc', '.cxx'):
            continue
        uncovered_functions.append({'path': path, 'line': region[0], 'name': function['name']})
    return {'areas': areas, 'files': files, 'unmeasured_translation_units': unmeasured,
            'uncovered_functions': sorted(uncovered_functions, key=lambda row: (row['path'], row['line'], row['name']))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--optimization', type=int, choices=(0, 1, 2, 3), default=0,
                        help='Clang optimization level; use 1 for expensive slow integration coverage')
    parser.add_argument('--debug-info', choices=('full', 'line-tables-only'), default='full',
                        help='line-tables-only bounds CI object storage while preserving coverage mapping')
    parser.add_argument('--profile-pool-size', type=int, choices=range(10), default=0,
                        help='LLVM online merge pool per binary (1..9); 0 keeps per-process raw profiles')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/native-coverage')
    parser.add_argument('--filter', action='append', default=[])
    parser.add_argument('--exclude-tag', action='append', default=[])
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--llvm-profdata', default='llvm-profdata')
    parser.add_argument('--llvm-cov', default='llvm-cov')
    parser.add_argument('--timeout', type=int, help='explicit per-case timeout override for instrumented runs')
    parser.add_argument('--with-cli', action='store_true', help='also measure the production executable in a separate client report')
    parser.add_argument('--quick', action='store_true')
    parser.add_argument('--no-display', action='store_true')
    parser.add_argument('--fullscreen', action='store_true')
    parser.add_argument('--discard-merged-profiles', action='store_true',
                        help='discard raw profiles after successful tests, merge, export and HTML; retain failed-run inputs')
    parser.add_argument('--stream-logs', action='store_true',
                        help='also stream command output to CI logs before artifact upload')
    parser.add_argument('--coverage-map', type=Path,
                        help='also record which source files each test case executes (test/select_affected.py)')
    parser.add_argument('-j', '--jobs', type=int, default=4)
    args = parser.parse_args()
    default_build = 'native-coverage' if args.optimization == 0 else f'native-coverage-o{args.optimization}'
    build = (args.build_dir or ROOT / 'build' / default_build).resolve()
    if (ROOT / 'build') not in build.parents:
        parser.error('--build-dir must be a dedicated directory under build/ in the repository')
    output = args.output.resolve() / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8])
    output.mkdir(parents=True)
    manifest = {'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
                'platform': platform.platform(), 'build': str(build), 'commands': [],
                'tools': {},
                'selection': vars(args) | {'build_dir': str(build), 'output': str(output)}, 'binaries': {}}

    def run(command, log, env=None):
        manifest['commands'].append(command)
        return run_logged(command, output / log, env=env, stream_logs=args.stream_logs)

    flags = instrumentation_flags(args.optimization, args.debug_info)
    # The plain debug tier (no dev_fast) with instrumented C++; the QuickJS and
    # openlibm C sources stay uninstrumented, as they always have been.
    command = [sys.executable, str(ROOT / 'tools/dev_build.py'), '--generator=cmake', f'-j{args.jobs}',
               'dev_fast=0', 'linker=default', f'--build={build}', 'tests',
               f'CC={args.cc}', f'CXX={args.cxx}', f'CXXFLAGS={flags}',
               'LINKFLAGS=-g -fprofile-instr-generate']
    if args.with_cli:
        command.append(str(build / 'src/glob2'))
    failed = 0
    try:
        for name in (args.cxx,args.llvm_profdata,args.llvm_cov):
            manifest['tools'][name] = subprocess.check_output(
                tool(name)+['--version'],text=True,stderr=subprocess.STDOUT).strip()
        failed = run(command, 'build.log')
        if failed:
            raise RuntimeError('coverage build failed; see build.log')
        case_maps = []
        for kind in (('unit', 'engine', 'client') if args.with_cli else ('unit', 'engine')):
            directory = output / kind
            raw = directory / 'raw'
            raw.mkdir(parents=True)
            env = os.environ.copy()
            env['LLVM_PROFILE_FILE'] = str(raw / profile_pattern(args.profile_pool_size))
            command = [sys.executable, str(ROOT / 'test/run_tests.py'), '--tier', 'display' if args.quick else 'full', '--build-dir', str(build),
                       '--binary', kind, '-j', str(args.jobs), '--display-jobs', '1',
                       '--exclude-tag', 'network', '--junit', str(directory / 'junit.xml'),
                       '--artifacts', str(directory / 'evidence')]
            if args.timeout:
                command += ['--timeout', str(args.timeout)]
            for selection in args.filter:
                command += ['--filter', selection]
            for tag in args.exclude_tag:
                command += ['--exclude-tag', tag]
            for option in ('no_display', 'fullscreen'):
                if getattr(args, option):
                    command.append('--' + option.replace('_', '-'))
            if args.coverage_map and kind != 'client':
                command += ['--profile-per-job', str(raw), '--llvm-profdata', tool(args.llvm_profdata)[-1],
                            '--llvm-cov', tool(args.llvm_cov)[-1]]
            if kind == 'client':
                command = [sys.executable, str(ROOT / 'test/test_cli_smoke.py'),
                           '--binary', str(build / 'src/glob2'), '--junit', str(directory / 'junit.xml'),
                           '--artifacts', str(directory / 'evidence')]
            status = run(command, f'{kind}/tests.log', env)
            test_status = status
            failed |= status
            # --coverage-map leaves one merged profile per test job instead of raw profiles.
            profiles = sorted(raw.glob('*.profraw')) + sorted(raw.glob('job-*.profdata'))
            if not profiles:
                raise RuntimeError(f'{kind}: no profiles were produced')
            # A kind's run can cover a companion module binary (unit + map, engine + sim).
            # Each binary gets its own profile and report: functions compiled into both
            # carry different hashes, and llvm-cov reports a merged profile as mismatched.
            binaries = kind_binaries(build, kind)
            html_status = 0
            for index, binary in enumerate(binaries):
                label = binary_label(binary, kind)
                report = directory if index == 0 else output / label
                report.mkdir(parents=True, exist_ok=True)
                selected = profiles_for_binary(profiles, binary, index == 0, tool(args.llvm_profdata))
                if not selected:
                    if index == 0:
                        raise RuntimeError(f'{kind}: no profiles were produced by {binary.name}')
                    continue
                relative = report.relative_to(output).as_posix()
                profile = report / 'coverage.profdata'
                status = run(tool(args.llvm_profdata) + ['merge', '-sparse', *map(str, selected), '-o', str(profile)],
                             f'{relative}/merge.log')
                if status:
                    raise RuntimeError(f'{label}: profile merge failed')
                covargs = [str(binary), f'-instr-profile={profile}', f'-ignore-filename-regex={IGNORE}']
                warnings = report / 'export.log'
                with (report / 'coverage.json').open('w') as stream, warnings.open('w') as err:
                    command = tool(args.llvm_cov) + ['export', *covargs, '-skip-expansions']
                    manifest['commands'].append(command)
                    if args.stream_logs:
                        print(f'Coverage JSON export ({label}): {shlex.join(command)}', flush=True)
                    result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=err)
                if result.returncode or warnings.read_text().strip():
                    raise RuntimeError(f'{label}: coverage diagnostics require investigation; see {relative}/export.log')
                summary = summarize(json.loads((report / 'coverage.json').read_text()))
                (report / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
                html_status |= run(tool(args.llvm_cov) + ['show', *covargs, '-format=html',
                                   f'-output-dir={report / "html"}', '-show-branches=count'], f'{relative}/html.log')
                manifest['binaries'][label] = {'test_exit': test_status, 'profiles': len(selected)}
            failed |= html_status
            if args.coverage_map and kind != 'client':
                import case_coverage
                case_maps.append(directory / 'coverage-map.json.gz')
                case_coverage.write_map(raw, binaries, case_maps[-1], tool(args.llvm_profdata)[-1], tool(args.llvm_cov)[-1])
            if args.discard_merged_profiles:
                manifest['binaries'][binary_label(binaries[0], kind)].update(compact_profiles(
                    profiles, successful=test_status == 0 and html_status == 0))
        if args.coverage_map and case_maps:
            import case_coverage
            case_coverage.combine_maps(case_maps, args.coverage_map)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        manifest['error'] = str(error)
        print(error, file=sys.stderr)
        failed = 1
    finally:
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2, default=str) + '\n')
        print(f'Coverage evidence: {output}', flush=True)
    return int(bool(failed))


if __name__ == '__main__':
    raise SystemExit(main())
