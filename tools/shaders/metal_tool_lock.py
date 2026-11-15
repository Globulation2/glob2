# SPDX-License-Identifier: GPL-3.0-or-later
"""Write or check the trusted Metal tool lock for offline SDL GPU shader compilation.

The lock (platforms/mobile/metal-tool-lock.json) is the trust anchor for the
Apple compiler that turns translated shader sources into the .metallib files an
iOS device build ships. It records the exact `xcodebuild -version` output, the
SDK name and version, and SHA-256 digests of the resolved `metal` and `metallib`
executables, in exactly the format tools/shaders/generate.py compile_metal reads.

Run it on a Mac with the Xcode pinned in platforms/mobile/toolchain.json:

    python3 tools/shaders/metal_tool_lock.py --output metal-tool-lock.json
    python3 tools/shaders/metal_tool_lock.py --check

Writing never replaces the committed lock in place; a reviewed pull request does.
`--check` exits non-zero and prints each differing field when the local
toolchain no longer matches the lock (for example after a runner image update).
"""
import argparse
import difflib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LOCK = ROOT / 'platforms/mobile/metal-tool-lock.json'
WORKFLOW = '.github/workflows/ios-metal-tool-lock.yml'


def _generator():
    spec = importlib.util.spec_from_file_location('glob2_renderer_shaders', ROOT / 'tools/shaders/generate.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generator = _generator()


def render(lock):
    """Canonical JSON text: the compile_metal keys in a fixed order, nothing else."""
    return json.dumps({key: lock[key] for key in generator.METAL_TOOL_LOCK_KEYS}, indent=2) + '\n'


def observe(sdk, root=ROOT, developer_dir=None):
    """The lock the selected toolchain would produce; refuses an Xcode other than the pin."""
    lock, _ = generator.observe_metal_tools(sdk, root, developer_dir)
    return lock


def report(lock_path, lock, observed):
    """Human-readable differences, or an empty string when the lock matches."""
    differences = generator.metal_tool_lock_differences(lock, observed)
    if not differences:
        return ''
    lines = ['Metal tool lock ' + str(lock_path) + ' does not match this toolchain:']
    for key, locked, found in differences:
        lines.append('  ' + key + ':')
        for label, value in (('locked', locked), ('observed', found)):
            text = '<missing>' if value is None else str(value)
            lines.append('    ' + (label + ':').ljust(10) + text.replace('\n', ' | '))
    if isinstance(lock, dict):
        expected = json.dumps(lock, indent=2, sort_keys=True).splitlines()
        actual = json.dumps(observed, indent=2, sort_keys=True).splitlines()
        lines += [''] + list(difflib.unified_diff(expected, actual, 'committed lock', 'this toolchain', lineterm=''))
    lines += ['', 'A mismatch means the Xcode, SDK or Metal compiler on this machine is not the one the '
              'repository reviewed, so no Metal library is compiled with it.', generator.METAL_TOOL_LOCK_REMEDY]
    return '\n'.join(lines)


def annotate(message):
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        print('::error title=Metal tool lock::' + message.splitlines()[0] + ' Run ' + WORKFLOW +
              ' and update platforms/mobile/metal-tool-lock.json in a pull request.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--output', type=Path, help='Write a new lock here (never the committed lock in place)')
    mode.add_argument('--check', action='store_true', help='Verify --lock against the local toolchain')
    parser.add_argument('--lock', type=Path, default=DEFAULT_LOCK, help='Lock to check (default: %(default)s)')
    parser.add_argument('--sdk', default='iphoneos', choices=('iphoneos', 'iphonesimulator', 'macosx'))
    parser.add_argument('--developer-dir', help='Selected Xcode Developer directory, without changing the system selection')
    args = parser.parse_args(argv)
    if args.output and args.output.resolve() == DEFAULT_LOCK.resolve():
        parser.error('write to a scratch path and commit the reviewed lock in a pull request')
    try:
        observed = observe(args.sdk, ROOT, args.developer_dir)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        message = 'Cannot observe the pinned Metal toolchain: ' + str(error)
        annotate(message)
        print(message, file=sys.stderr)
        return 1
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(render(observed))
        print(render(observed), end='')
        return 0
    if not args.lock.is_file():
        message = ('No Metal tool lock at ' + str(args.lock) + '. Run ' + WORKFLOW + ' and commit its reviewed '
                   'artifact as platforms/mobile/metal-tool-lock.json.')
        annotate(message)
        print(message, file=sys.stderr)
        return 2
    try:
        lock = json.loads(args.lock.read_text())
    except ValueError as error:
        lock = {'<unreadable>': str(error)}
    problem = report(args.lock, lock, observed)
    if problem:
        annotate(problem)
        print(problem, file=sys.stderr)
        return 1
    print('Metal tool lock ' + str(args.lock) + ' matches ' + observed['xcodeVersion'].replace('\n', ', ') +
          ', ' + args.sdk + ' ' + observed['sdkVersion'] + '.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
