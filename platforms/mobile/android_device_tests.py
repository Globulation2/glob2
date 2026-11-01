#!/usr/bin/env python3
"""Run the cross-compiled doctest binaries on one explicitly selected Android device.

Uses disposable /data/local/tmp directories, never the installed app's data.
These are CPU/input/persistence tests with SDL dummy drivers ([display] cases are
excluded); they complement, not replace, installed-APK keyboard, renderer,
lifecycle and interaction checks.
"""
import argparse
import json
import hashlib
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import uuid
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "test"))
from build_provenance import source_identity, build_issues

ROOT = Path(__file__).resolve().parents[1]
BINARIES = ('glob2-unit-tests', 'glob2-engine-tests')



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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--adb-port', type=int, default=5037, help='Explicit ADB server port for an isolated emulator')
    parser.add_argument('--android-sdk', type=Path, required=True)
    parser.add_argument('--arch', choices=('arm64-v8a', 'armeabi-v7a', 'x86_64'), default='arm64-v8a')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/android/device-tests')
    parser.add_argument('--binary', action='append', choices=BINARIES)
    parser.add_argument('--mobile-deps', type=Path, help='Dependency prefix used to link the test binaries')
    parser.add_argument('--suite', help='doctest suite selector, e.g. JavaScript*')
    parser.add_argument('--keep-remote', action='store_true')
    args = parser.parse_args()
    adb = [str(args.android_sdk / 'platform-tools/adb'), '-P', str(args.adb_port), '-s', args.serial]
    def command(*parts, **kwargs):
        return subprocess.run(adb + list(parts), check=True, **kwargs)
    if subprocess.check_output(adb + ['get-state'], text=True).strip() != 'device':
        raise RuntimeError('Selected device is not authorized')
    abis = subprocess.check_output(adb + ['shell', 'getprop', 'ro.product.cpu.abilist'], text=True)
    if args.arch not in abis.strip().split(','):
        raise RuntimeError('Device does not support ' + args.arch)
    build = ROOT / f'build/android/device/{args.arch}/24/client/release'
    names = args.binary or BINARIES
    for name in names:
        if not (build / 'tests' / name).is_file():
            raise RuntimeError('Build android-tests before running: ' + name)
    ndk = json.loads((ROOT / 'mobile/toolchain.json').read_text())['android']['ndk']
    prebuilt = next((args.android_sdk / 'ndk' / ndk / 'toolchains/llvm/prebuilt').iterdir())
    remote = '/data/local/tmp/glob2-tests-' + uuid.uuid4().hex
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {'serial': args.serial, 'arch': args.arch, 'remoteDirectory': remote,
               'mode': 'native Android CPU with SDL dummy video; no JVM or audio device',
               'suite': args.suite or '*', 'tests': [], 'retrievalErrors': [], 'remoteRetained': True}
    summary.update(source_identity())
    summary['compilerVersion'] = subprocess.check_output([str(prebuilt / 'bin/clang++'), '--version'], text=True).strip()
    summary['buildConfiguration'] = {name: (build / name).read_text()
                                     for name in ('identity.json', 'toolchain.json', 'options.json')
                                     if (build / name).is_file()}
    executed_hashes = {}
    summary['device'] = {key: subprocess.check_output(adb + ['shell', 'getprop', key], text=True).strip()
                         for key in ('ro.product.model', 'ro.build.version.release', 'ro.build.version.sdk', 'ro.product.cpu.abilist')}
    summary['fixtureHashes'] = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in sorted((ROOT / 'test/fixtures/javascript').rglob('*')) if p.is_file()}
    (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
    with tempfile.TemporaryDirectory(prefix='glob2-device-tests-') as directory:
        payload = Path(directory)
        for name in names:
            subprocess.run([str(prebuilt / 'bin/llvm-strip'), '--strip-debug', '-o',
                            str(payload / name), str(build / 'tests' / name)], check=True)
            (payload / name).chmod(0o755)
            executed_hashes[name] = hashlib.sha256((payload / name).read_bytes()).hexdigest()
        # Shell test executables do not require an APK or generated Gradle
        # assets. Stage native dependencies and repository fixtures directly.
        prefix = args.mobile_deps
        if prefix is None:
            candidates = [build / 'deps'] + list((build / 'vcpkg-installed').glob('glob2-*'))
            prefix = next((p for p in candidates if (p / 'manifest.json').is_file()), None)
        if prefix is None:
            raise RuntimeError('Specify --mobile-deps for the linked dependency prefix')
        dependencies = json.loads((prefix / 'manifest.json').read_text())
        summary['dependencies'] = dependencies
        for filename in dependencies['archives']:
            if filename.endswith('.so'):
                shutil.copy2(prefix / filename, payload / Path(filename).name)
        triple = {'arm64-v8a': 'aarch64-linux-android', 'armeabi-v7a': 'arm-linux-androideabi',
                  'x86_64': 'x86_64-linux-android'}[args.arch]
        shutil.copy2(prebuilt / 'sysroot/usr/lib' / triple / 'libc++_shared.so', payload / 'libc++_shared.so')
        for name in ('data', 'maps', 'campaigns', 'scripts'):
            shutil.copytree(ROOT / name, payload / name)
        # Fixture-driven cases resolve everything through glob2test::sourceRoot().
        shutil.copytree(ROOT / 'games', payload / 'games')
        shutil.copytree(ROOT / 'test/fixtures', payload / 'test/fixtures')
        shutil.copytree(ROOT / 'test/maxima/fixtures', payload / 'test/maxima/fixtures')
        shutil.copytree(ROOT / 'examples/javascript', payload / 'examples/javascript')
        command('push', str(payload), remote, stdout=subprocess.DEVNULL)
    for name in names:
        profile = remote + '/profiles/' + name
        # Cases that open a window are for the desktop runner; keep the rest in-process.
        extra = ['-tce=*[display*', '-r=junit', '-o=' + remote + '/' + name + '.xml']
        if args.suite:
            extra += ['--test-suite=' + args.suite]
        timeout = 600
        invocation = ['env', 'LD_LIBRARY_PATH=' + remote, 'SDL_VIDEODRIVER=dummy',
                      'SDL_RENDER_DRIVER=software', 'SDL_AUDIODRIVER=dummy',
                      'GLOB2_USER_DATA_DIR=' + profile, 'GLOB2_ASSET_DIR=' + remote,
                      'GLOB2_TEST_SOURCE_ROOT=' + remote,
                      'GLOB2_TEST_ARTIFACTS_ROOT=' + remote + '/artifacts',
                      'timeout', '-k', '5', str(timeout), './' + name] + extra
        shell = ('cd ' + shlex.quote(remote) + ' && mkdir -p ' + shlex.quote(profile) +
                 ' && ' + shlex.join(invocation))
        # Stream to disk so long-running suites remain diagnosable mid-run.
        with (output / (name + '.log')).open('w') as log:
            try:
                result = subprocess.run(adb + ['shell', shell], stdout=log,
                                        stderr=subprocess.STDOUT, timeout=timeout + 10)
                code = result.returncode
            except subprocess.TimeoutExpired as error:
                code = 124
                log.write(str(error) + '\n')
        retrieve(adb, remote + '/' + name + '.xml', output / (name + '.xml'),
                 summary['retrievalErrors'], xml=True)
        proofs = [json.loads(line.partition('GLOB2_TEST_PROVENANCE ')[2])
                  for line in (output / (name + '.log')).read_text().splitlines()
                  if line.startswith('GLOB2_TEST_PROVENANCE ')]
        proof = proofs[0] if len(proofs) == 1 else {}
        digest = hashlib.sha256((build / 'tests' / name).read_bytes()).hexdigest()
        summary['tests'].append({'name': name, 'exitCode': code, 'build': proof,
                                 'provenanceIssues': build_issues(proof, summary), 'binarySha256': digest, 'executedBinarySha256': executed_hashes[name]})
        (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(('PASS' if code == 0 and not summary['tests'][-1]['provenanceIssues']
               and not summary['retrievalErrors'] else 'FAIL') + ': ' + name, flush=True)
    # Retrieve all corpus bits, saves and traces, including failed-case evidence.
    retrieve(adb, remote + '/artifacts', output / 'corpus', summary['retrievalErrors'])
    failed = bool(summary['retrievalErrors']) or any(
        test['exitCode'] or test['provenanceIssues'] for test in summary['tests'])
    if not args.keep_remote and not failed:
        command('shell', 'rm -rf ' + shlex.quote(remote))
        summary['remoteRetained'] = False
    (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
    # Keep remote fixtures for failure diagnosis. Never remove application data.
    if failed:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
