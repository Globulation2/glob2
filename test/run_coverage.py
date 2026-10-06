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
import subprocess
import sys
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


def compact_profiles(profiles, successful):
    """Drop redundant inputs only after passing tests and complete report generation."""
    if not successful:
        return {'raw_profiles_retained': True}
    size = sum(path.stat().st_size for path in profiles)
    for path in profiles:
        path.unlink()
    return {'raw_profiles_retained': False, 'raw_profile_bytes_removed': size}


def area(path):
    parts = Path(path).parts
    if parts[0] == 'src' and len(parts) > 2:
        return '/'.join(parts[:3] if parts[1] in ('ai', 'map') and len(parts) > 3 else parts[:2])
    return parts[0]


def summarize(export, root=ROOT):
    """Retain raw covered/count values; never average percentages or combine binaries."""
    files, areas = [], {}
    for item in export['data'][0]['files']:
        try:
            relative = Path(item['filename']).resolve().relative_to(root.resolve())
        except ValueError:
            continue
        if relative.parts[0] not in ('src', 'libgag', 'libusl', 'natsort', 'mobile'):
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
                        for directory in ('src', 'libgag', 'libusl', 'natsort', 'mobile')
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
        with (output / log).open('w') as stream:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
        return result.returncode

    flags = f'-g -O{args.optimization} -fprofile-instr-generate -fcoverage-mapping -DGLOB2_TEST_COVERAGE'
    command = ['scons', f'-j{args.jobs}', 'release=0', 'server=0', f'--build={build}', 'tests',
               f'CC={args.cc}', f'CXX={args.cxx}', f'CFLAGS={flags}', f'CXXFLAGS={flags}',
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
        for kind in (('unit', 'engine', 'client') if args.with_cli else ('unit', 'engine')):
            directory = output / kind
            raw = directory / 'raw'
            raw.mkdir(parents=True)
            env = os.environ.copy()
            env['LLVM_PROFILE_FILE'] = str(raw / '%m-%p.profraw')
            command = [sys.executable, str(ROOT / 'test/run_tests.py'), '--build-dir', str(build),
                       '--binary', kind, '-j', str(args.jobs), '--display-jobs', '1',
                       '--exclude-tag', 'network', '--junit', str(directory / 'junit.xml'),
                       '--artifacts', str(directory / 'evidence')]
            if args.timeout:
                command += ['--timeout', str(args.timeout)]
            for selection in args.filter:
                command += ['--filter', selection]
            for tag in args.exclude_tag:
                command += ['--exclude-tag', tag]
            for option in ('quick', 'no_display', 'fullscreen'):
                if getattr(args, option):
                    command.append('--' + option.replace('_', '-'))
            if kind == 'client':
                command = [sys.executable, str(ROOT / 'test/test_cli_smoke.py'),
                           '--binary', str(build / 'src/glob2'), '--junit', str(directory / 'junit.xml'),
                           '--artifacts', str(directory / 'evidence')]
            status = run(command, f'{kind}/tests.log', env)
            test_status = status
            failed |= status
            profiles = sorted(raw.glob('*.profraw'))
            if not profiles:
                raise RuntimeError(f'{kind}: no profiles were produced')
            profile = directory / 'coverage.profdata'
            status = run(tool(args.llvm_profdata) + ['merge', '-sparse', *map(str, profiles), '-o', str(profile)], f'{kind}/merge.log')
            if status:
                raise RuntimeError(f'{kind}: profile merge failed')
            binary = build / 'src/glob2' if kind == 'client' else build / 'test' / f'glob2-{kind}-tests'
            covargs = [str(binary), f'-instr-profile={profile}', f'-ignore-filename-regex={IGNORE}']
            warnings = directory / 'export.log'
            with (directory / 'coverage.json').open('w') as stream, warnings.open('w') as err:
                command = tool(args.llvm_cov) + ['export', *covargs, '-skip-expansions']
                manifest['commands'].append(command)
                result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=err)
            if result.returncode or warnings.read_text().strip():
                raise RuntimeError(f'{kind}: coverage diagnostics require investigation; see export.log')
            summary = summarize(json.loads((directory / 'coverage.json').read_text()))
            (directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
            html_status = run(tool(args.llvm_cov) + ['show', *covargs, '-format=html',
                          f'-output-dir={directory / "html"}', '-show-branches=count'], f'{kind}/html.log')
            failed |= html_status
            manifest['binaries'][kind] = {'test_exit': test_status, 'profiles': len(profiles)}
            if args.discard_merged_profiles:
                manifest['binaries'][kind].update(compact_profiles(
                    profiles, successful=test_status == 0 and html_status == 0))
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        manifest['error'] = str(error)
        print(error, file=sys.stderr)
        failed = 1
    finally:
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'Coverage evidence: {output}', flush=True)
    return int(bool(failed))


if __name__ == '__main__':
    raise SystemExit(main())
