# SPDX-License-Identifier: GPL-3.0-or-later
"""Write or check the trusted Metal tool lock for SDL GPU shader compilation.

The lock (buildsys/metal-tool-lock.json) is the trust anchor for the Apple
compilers that turn translated shader sources into the .metallib files every
macOS and iOS release ships. It records the exact `xcodebuild -version` output
and, per SDK (macosx, iphoneos), the SDK version and SHA-256 digests of the
resolved `metal` and `metallib` executables, for every reviewed toolchain (each
macOS release runner image, and the pinned Xcode that iOS releases need):

    {"toolchains": [{"xcodeVersion": "...", "sdks": {"macosx": {"sdkVersion": ...,
     "metalSha256": ..., "metallibSha256": ...}, "iphoneos": {...}}}, ...]}

On each Mac (the Apple Metal tool lock workflow runs it on every runner image):

    python3 tools/shaders/metal_tool_lock.py --output toolchain.json
    python3 tools/shaders/metal_tool_lock.py --combine buildsys/metal-tool-lock.json toolchain.json
    python3 tools/shaders/metal_tool_lock.py --check

Writing never replaces the committed lock in place; a reviewed pull request does.
`--check` exits non-zero and prints each differing field when the local
toolchain no longer matches the lock (for example after a runner image update).
Development builds on a Mac with another Xcode do not need this: they record a
development-only lock in their build directory, which release builds refuse.
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
WORKFLOW = '.github/workflows/apple-metal-tool-lock.yml'
SDKS = ('macosx', 'iphoneos')


def _generator():
    spec = importlib.util.spec_from_file_location('glob2_renderer_shaders', ROOT / 'tools/shaders/generate.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generator = _generator()
DEFAULT_LOCK = ROOT / generator.COMMITTED_METAL_TOOL_LOCK


def observe(sdks=SDKS, root=ROOT, developer_dir=None):
    """This Mac's toolchain entry. iOS entries require the pinned Xcode; macOS ones any Xcode."""
    toolchain = {'xcodeVersion': None, 'sdks': {}}
    for sdk in sdks:
        observed, _ = generator.observe_metal_tools(sdk, root, developer_dir, require_pin=sdk != 'macosx')
        toolchain['xcodeVersion'] = observed['xcodeVersion']
        toolchain['sdks'][sdk] = {key: observed[key] for key in generator.METAL_SDK_KEYS}
    return toolchain


def _canonical(toolchain):
    return {'xcodeVersion': toolchain['xcodeVersion'],
            'sdks': {sdk: {key: toolchain['sdks'][sdk][key] for key in generator.METAL_SDK_KEYS}
                     for sdk in sorted(toolchain['sdks'])}}


def combine(locks):
    """One lock holding every distinct toolchain of the given locks, in a stable order."""
    seen, toolchains = set(), []
    for lock in locks:
        for toolchain in lock.get('toolchains', []):
            canonical = _canonical(toolchain)
            key = json.dumps(canonical, sort_keys=True)
            if key not in seen:
                seen.add(key)
                toolchains.append(canonical)
    toolchains.sort(key=lambda toolchain: json.dumps(toolchain, sort_keys=True))
    return {'toolchains': toolchains}


def render(lock):
    """Canonical JSON text: the known keys in a fixed order, nothing else."""
    return json.dumps(combine([lock]), indent=2) + '\n'


def differences(lock, observed):
    """(field, locked, observed) between observed and the nearest reviewed toolchain; [] on a match."""
    toolchains = lock.get('toolchains') if isinstance(lock, dict) else None
    if not isinstance(toolchains, list) or not toolchains:
        return [('<lock>', lock, 'an object with a non-empty toolchains list')]
    nearest = None
    for toolchain in toolchains:
        found = []
        if not isinstance(toolchain, dict) or not isinstance(toolchain.get('sdks'), dict):
            found.append(('<toolchain>', toolchain, 'an object with xcodeVersion and sdks'))
        else:
            found += [(key, toolchain.get(key), None) for key in sorted(set(toolchain) - {'xcodeVersion', 'sdks'})]
            if toolchain.get('xcodeVersion') != observed['xcodeVersion']:
                found.append(('xcodeVersion', toolchain.get('xcodeVersion'), observed['xcodeVersion']))
            for sdk in sorted(observed['sdks']):
                locked, seen = toolchain['sdks'].get(sdk) or {}, observed['sdks'][sdk]
                for key in sorted(set(locked) | set(seen)):
                    if locked.get(key) != seen.get(key):
                        found.append((sdk + '.' + key, locked.get(key), seen.get(key)))
        if not found:
            return []
        if nearest is None or len(found) < len(nearest):
            nearest = found
    return nearest


def report(lock_path, lock, observed):
    """Human-readable differences from the nearest reviewed toolchain, or '' on a match."""
    found = differences(lock, observed)
    if not found:
        return ''
    lines = ['No reviewed toolchain in ' + str(lock_path) + ' matches this Mac; nearest differences:']
    for key, locked, seen in found:
        lines.append('  ' + key + ':')
        for label, value in (('locked', locked), ('observed', seen)):
            text = '<missing>' if value is None else str(value)
            lines.append('    ' + (label + ':').ljust(10) + text.replace('\n', ' | '))
    lines += ['', 'This toolchain entry would match:', json.dumps(_canonical(observed), indent=2), '',
              'A mismatch means the Xcode, SDK or Metal compiler on this machine is not one the repository '
              'reviewed, so no release Metal library is compiled with it.', generator.METAL_TOOL_LOCK_REMEDY]
    return '\n'.join(lines)


def annotate(message):
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        print('::error title=Metal tool lock::' + message.splitlines()[0] + ' Run ' + WORKFLOW +
              ' and update ' + str(generator.COMMITTED_METAL_TOOL_LOCK) + ' in a pull request.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--output', type=Path, help="Write this Mac's toolchain as a lock here (never the committed lock)")
    mode.add_argument('--check', action='store_true', help='Verify that --lock lists this Mac\'s toolchain')
    mode.add_argument('--combine', type=Path, nargs='+', metavar='LOCK',
                      help='Print one lock holding every toolchain of these locks (for review)')
    parser.add_argument('--lock', type=Path, default=DEFAULT_LOCK, help='Lock to check (default: %(default)s)')
    parser.add_argument('--sdk', action='append', choices=SDKS,
                        help='SDKs to observe or check (default: all of ' + ', '.join(SDKS) + ')')
    parser.add_argument('--developer-dir', help='Selected Xcode Developer directory, without changing the system selection')
    args = parser.parse_args(argv)
    if args.combine:
        print(render(combine([json.loads(path.read_text()) for path in args.combine])), end='')
        return 0
    sdks = tuple(args.sdk or SDKS)
    if args.output and args.output.resolve() == DEFAULT_LOCK.resolve():
        parser.error('write to a scratch path and commit the reviewed lock in a pull request')
    try:
        observed = observe(sdks, ROOT, args.developer_dir)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        message = 'Cannot observe the Metal toolchain: ' + str(error)
        annotate(message)
        print(message, file=sys.stderr)
        return 1
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(render({'toolchains': [observed]}))
        print(render({'toolchains': [observed]}), end='')
        return 0
    if not args.lock.is_file():
        message = ('No Metal tool lock at ' + str(args.lock) + '. Run ' + WORKFLOW + ' and commit its reviewed '
                   'artifact as ' + str(generator.COMMITTED_METAL_TOOL_LOCK) + '.')
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
    print('Metal tool lock ' + str(args.lock) + ' lists ' + observed['xcodeVersion'].replace('\n', ', ') + ', ' +
          ', '.join(sdk + ' ' + observed['sdks'][sdk]['sdkVersion'] for sdk in sdks) + '.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
