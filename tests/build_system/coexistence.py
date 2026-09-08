#!/usr/bin/env python3
"""Build from one checkout and assert incremental configurations survive each other."""
import argparse
import concurrent.futures
import hashlib
from pathlib import Path
import platform
import subprocess


def run(arguments):
    subprocess.run(['scons', 'release=1', '-j2', '--debug=explain', *arguments], check=True)


def snapshot(directory):
    # Signature DB access and logs can change on no-op builds; compilation outputs cannot.
    files = [p for p in directory.rglob('*') if p.is_file() and
             (p.suffix in ('.o', '.a', '.wasm', '.js', '.html', '.data') or
              p.name in ('BuildConfig.h', 'glob2', 'compile_commands.json'))]
    if not files:
        raise AssertionError(f'No build outputs in {directory}')
    # SCons regenerates its compilation database on every invocation by design.
    return {str(p): (None if p.name == 'compile_commands.json' else p.stat().st_mtime_ns,
                     hashlib.sha256(p.read_bytes()).hexdigest()) for p in files}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--order', choices=('native-first', 'web-first', 'concurrent'), required=True)
    args = parser.parse_args()
    tracked = subprocess.check_output(['git', 'diff', 'HEAD', '--binary'])
    native, web = [], ['target=web']
    if args.order == 'concurrent':
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results = [pool.submit(run, target) for target in (native, web)]
            for result in results:
                result.result()
    else:
        for target in ((native, web) if args.order == 'native-first' else (web, native)):
            run(target)
    directories = [Path('build') / platform.system().lower() / 'client/release',
                   Path('build/emscripten/client/release')]
    before = [snapshot(p) for p in directories]
    for target in (native, web, native, web):
        run(target)
        after = [snapshot(p) for p in directories]
        changes = []
        for previous, current in zip(before, after):
            for path in sorted(previous.keys() | current.keys()):
                if previous.get(path) == current.get(path):
                    continue
                if path not in previous:
                    reason = 'added'
                elif path not in current:
                    reason = 'removed'
                elif previous[path][1] != current[path][1]:
                    reason = 'content changed'
                else:
                    reason = 'timestamp changed (content unchanged)'
                changes.append(f'{path}: {reason}')
        assert not changes, ('A no-op build changed compilation outputs after ' +
                             repr(target or ['target=native']) + ':\n' + '\n'.join(changes))
    assert subprocess.check_output(['git', 'diff', 'HEAD', '--binary']) == tracked, 'Build changed tracked source files'
    for filename in ('config.h', 'options_cache.py', 'compile_commands.json'):
        assert not Path(filename).exists(), f'Global build state created: {filename}'


if __name__ == '__main__':
    main()
