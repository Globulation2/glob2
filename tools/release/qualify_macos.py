#!/usr/bin/env python3
"""Qualify final direct-download Mac bytes on native hosts; no signing keys."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile

FIXTURE_SHA256 = 'bdbc7f65ed3fc96b040ece0213050b92f67c063d5e3d2f82422b3530e22347d3'
CASES = ('direct-600', 'repeat-600', 'save-300', 'resume-600', 'full-match')


def trace(path):
    lines = path.read_text().splitlines()
    rows = [tuple(map(int, line.split())) for line in lines]
    if not rows or any(len(row) != 2 for row in rows):
        raise ValueError('Malformed simulation trace')
    if [row[0] for row in rows] != list(range(rows[0][0], rows[-1][0] + 1)):
        raise ValueError('Simulation trace has duplicate, missing or reordered ticks')
    return rows


def verify_cases(root):
    traces = {name: trace(root / 'cases' / name / 'world.checksums') for name in CASES}
    results = {name: json.loads((root / 'cases' / name / 'result.json').read_text()) for name in CASES}
    for name in CASES:
        result, rows = results[name], traces[name]
        # world.checksums records the state before each step; result.json
        # describes the state after the final executed step.
        if (result.get('status') != 'completed' or result.get('ticks') != rows[-1][0] + 1 or
                result.get('initialChecksum') != rows[0][1] or
                type(result.get('finalChecksum')) is not int or not 0 <= result['finalChecksum'] < 2**32):
            raise ValueError('Incomplete simulation or final-byte trace/result mismatch')
    if traces['direct-600'][0][0] != 0 or traces['direct-600'][-1][0] != 599:
        raise ValueError('Direct simulation did not execute all 600 ticks')
    if (traces['repeat-600'] != traces['direct-600'] or
            results['repeat-600']['finalChecksum'] != results['direct-600']['finalChecksum']):
        raise ValueError('Repeated simulation diverged')
    if (traces['save-300'] != traces['direct-600'][:300] or
            results['save-300']['finalChecksum'] != traces['direct-600'][300][1]):
        raise ValueError('Save simulation diverged')
    if (traces['resume-600'] != traces['direct-600'][300:] or
            results['resume-600']['finalChecksum'] != results['direct-600']['finalChecksum']):
        raise ValueError('Resumed save diverged')
    full = results['full-match']
    if traces['full-match'][0][0] != 0 or not 600 < full['ticks'] < 90000 or full.get('unresolved') is not False:
        raise ValueError('Full match did not end naturally')
    replay = root / 'cases/full-match/game.replay'
    if not replay.is_file() or replay.stat().st_size == 0:
        raise ValueError('Completed match did not produce a replay')
    return {name: {'ticks': results[name]['ticks'], 'finalChecksum': results[name]['finalChecksum'],
                   'traceSha256': hashlib.sha256((root / 'cases' / name / 'world.checksums').read_bytes()).hexdigest()}
            for name in CASES}


def compare(root, output):
    reports = {arch: json.loads((root / ('qualification-macos-' + arch) / 'report.json').read_text())
               for arch in ('arm64', 'x86_64')}
    for field in ('tag', 'sourceCommit', 'fixtureSha256', 'sourceRunId', 'verifierCommit', 'qualificationRunId'):
        if reports['arm64'][field] != reports['x86_64'][field]:
            raise ValueError('Qualification mixes source revisions, versions or fixtures')
    # Architecture builds retain their own compilation times in the reports.
    # Compare every compatibility field, excluding only that build timestamp.
    versions = [{key: value for key, value in report['version'].items() if key != 'compiled'}
                for report in reports.values()]
    if versions[0] != versions[1]:
        raise ValueError('Qualification mixes release or compatibility versions')
    for arch, report in reports.items():
        if (report['architecture'] != arch or report['hostArchitecture'] != arch or
                report['nativeHost'] is not True or report['publisherVerified'] is not True or
                report['fixtureSha256'] != FIXTURE_SHA256 or
                not re.fullmatch('[0-9a-f]{64}', report['finalDmgSha256'])):
            raise ValueError('Qualification did not execute on both native architectures')
    for name in CASES:
        rows = [trace(root / ('qualification-macos-' + arch) / 'cases' / name / 'world.checksums')
                for arch in ('arm64', 'x86_64')]
        for arch in reports:
            path = root / ('qualification-macos-' + arch) / 'cases' / name / 'world.checksums'
            if reports[arch]['cases'][name]['traceSha256'] != hashlib.sha256(path.read_bytes()).hexdigest():
                raise ValueError('Qualification report differs from final trace bytes')
        if rows[0] != rows[1] or reports['arm64']['cases'][name] != reports['x86_64']['cases'][name]:
            raise ValueError('Native Mac release architectures diverged')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'tag': reports['arm64']['tag'], 'sourceCommit': reports['arm64']['sourceCommit'],
                                  'nativeArchitectures': list(reports), 'allTickTracesEqual': True,
                                  'packages': {arch: report['finalDmgSha256'] for arch, report in reports.items()},
                                  'cases': reports['arm64']['cases'],
                                  'coverageLimits': 'CLI launch, actual save/load, repeatability and natural full match/replay creation. Interactive tutorial, audio, editor, updates and online require separate qualification.'}, indent=2) + '\n')


def qualify(args):
    if platform.system() != 'Darwin' or platform.machine() != args.architecture:
        raise ValueError('Qualification requires a native Mac host matching the package architecture')
    translated = subprocess.run(['sysctl', '-n', 'sysctl.proc_translated'], capture_output=True, text=True)
    if translated.returncode == 0 and translated.stdout.strip() != '0':
        raise ValueError('Translated execution cannot qualify a native release architecture')
    if not args.expected_team:
        raise ValueError('Missing private mirror MACOS_DOWNLOAD_TEAM_ID configuration')
    actual_source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.source_root, text=True).strip()
    if actual_source != args.source_commit:
        raise ValueError('Validator checkout differs from selected immutable source')
    if hashlib.sha256(args.fixture.read_bytes()).hexdigest() != FIXTURE_SHA256:
        raise ValueError('Qualification fixture changed')
    spec = importlib.util.spec_from_file_location('source_sign_macos', args.source_root / 'tools/release/sign_macos.py')
    signing = importlib.util.module_from_spec(spec); spec.loader.exec_module(signing)
    args.output.mkdir(parents=True, exist_ok=False)
    commands = []
    def run(argv, label, env=None, cwd=None, timeout=300):
        result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, env=env, cwd=cwd, timeout=timeout)
        (args.output / (label + '.stdout')).write_text(result.stdout)
        (args.output / (label + '.stderr')).write_text(result.stderr)
        commands.append({'argv': list(map(str, argv)), 'exitCode': result.returncode})
        if result.returncode:
            raise ValueError(label + ' failed; inspect runner diagnostics')
        return result.stdout
    run(['codesign', '--verify', '--strict', args.dmg], 'dmg-signature')
    run(['xcrun', 'stapler', 'validate', args.dmg], 'dmg-staple')
    run(['spctl', '--assess', '--type', 'open', '--context', 'context:primary-signature', args.dmg], 'dmg-gatekeeper')
    with tempfile.TemporaryDirectory(prefix='glob2-qualified-macos-') as temporary:
        temp = Path(temporary); mount = temp / 'mount'; mount.mkdir(); attached = False
        app = temp / 'installed with spaces/Glob2.app'; app.parent.mkdir()
        try:
            run(['hdiutil', 'attach', args.dmg.resolve(), '-mountpoint', mount, '-readonly', '-nobrowse'], 'mount'); attached = True
            inventory = [str(path.relative_to(mount / 'Glob2.app')) for path in signing.validate(mount / 'Glob2.app', args.architecture)]
            run(['ditto', mount / 'Glob2.app', app], 'install')
        finally:
            if attached: run(['hdiutil', 'detach', mount], 'unmount')
        run(['codesign', '--verify', '--deep', '--strict', app], 'app-signature')
        run(['codesign', '-dv', '--verbose=4', app], 'app-identity')
        identity = (args.output / 'app-identity.stderr').read_text()
        if ('Authority=Developer ID Application:' not in identity or
                f'TeamIdentifier={args.expected_team}\n' not in identity or 'runtime' not in identity or 'Timestamp=' not in identity):
            raise ValueError('Unexpected publisher, hardened runtime or signing timestamp')
        run(['xcrun', 'stapler', 'validate', app], 'app-staple')
        run(['spctl', '--assess', '--type', 'execute', app], 'app-gatekeeper')
        exe = app / 'Contents/MacOS/glob2'; home = temp / 'home'; home.mkdir(); work = temp / 'outside-checkout'; work.mkdir()
        env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / 'config'), XDG_DATA_HOME=str(home / 'data'), XDG_CACHE_HOME=str(home / 'cache'))
        version = json.loads(run([exe, 'info', 'version', '--format', 'json'], 'version', env, work))
        if version['version'] != args.tag.removeprefix('v'):
            raise ValueError('Installed release version differs from immutable tag')
        run([exe, 'info', 'paths', '--format', 'json'], 'paths', env, work)
        base = [exe, 'game', 'run', '--compute-threads', '1', '--profile', 'glob2-native-release-qualification', '--save', 'final', '--telemetry', 'checksums']
        setup = ['--map-file', args.fixture.resolve(), '--game-seed', '713', '--player', 'castor', '--player', 'cortex']
        for name, ticks in zip(CASES, (600, 600, 300, 600, 90000)):
            command = base + (['--load-game', (args.output / 'cases/save-300/final.game.gz').resolve()] if name == 'resume-600' else setup)
            command += ['--ticks', str(ticks), '--output-dir', (args.output / 'cases' / name).resolve()]
            if name == 'full-match': command += ['--write-replay']
            run(command, name, env, work, 1200)
        cases = verify_cases(args.output)
        report = {'tag': args.tag, 'sourceCommit': args.source_commit, 'architecture': args.architecture, 'nativeHost': True,
                  'hostOS': platform.mac_ver()[0], 'hostArchitecture': platform.machine(), 'translationProbe': translated.stdout.strip(),
                  'finalDmgSha256': hashlib.sha256(args.dmg.read_bytes()).hexdigest(), 'installedBinarySha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                  'fixtureSha256': FIXTURE_SHA256, 'version': version, 'publisherVerified': True, 'binaryInventory': inventory,
                  'cases': cases, 'commands': commands,
                  'coverageLimits': 'Native installed CLI qualification. Interactive tutorial, audio, editor, updates and online are separate.'}
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__); modes = parser.add_subparsers(dest='mode', required=True)
    run = modes.add_parser('run')
    for name in ('dmg', 'source-root', 'fixture', 'output'): run.add_argument('--' + name, type=Path, required=True)
    for name in ('tag', 'source-commit', 'expected-team'): run.add_argument('--' + name, required=True)
    run.add_argument('--architecture', choices=('arm64', 'x86_64'), required=True)
    comparison = modes.add_parser('compare'); comparison.add_argument('--evidence-root', type=Path, required=True); comparison.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'run': qualify(args)
    else: compare(args.evidence_root, args.output)
    print('Native final Mac package qualification passed')


if __name__ == '__main__': main()
