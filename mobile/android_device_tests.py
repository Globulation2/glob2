#!/usr/bin/env python3
"""Run cross-compiled regression suites on one explicitly selected Android device.

Uses disposable /data/local/tmp directories, never the installed app's data.
These are CPU/input/persistence tests with SDL dummy drivers; they complement,
not replace, installed-APK keyboard, renderer, lifecycle and interaction checks.
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

ROOT = Path(__file__).resolve().parents[1]
HARNESSES = ('TestsRunner', 'MobileInputHarness', 'ResponsiveMenuHarness',
             'MobilePresentationHarness', 'GameGUITouchHarness',
             'EngineSessionHarness', 'GameGUISelectionHarness',
             'TerrainResourcesHarness', 'TeamStatsSaveHarness')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--android-sdk', type=Path, required=True)
    parser.add_argument('--arch', choices=('arm64-v8a', 'armeabi-v7a', 'x86_64'), default='arm64-v8a')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/android/device-tests')
    parser.add_argument('--harness', action='append', choices=HARNESSES)
    args = parser.parse_args()
    adb = [str(args.android_sdk / 'platform-tools/adb'), '-s', args.serial]
    def command(*parts, **kwargs):
        return subprocess.run(adb + list(parts), check=True, **kwargs)
    if subprocess.check_output(adb + ['get-state'], text=True).strip() != 'device':
        raise RuntimeError('Selected device is not authorized')
    abis = subprocess.check_output(adb + ['shell', 'getprop', 'ro.product.cpu.abilist'], text=True)
    if args.arch not in abis.strip().split(','):
        raise RuntimeError('Device does not support ' + args.arch)
    build = ROOT / f'build/android/device/{args.arch}/26/client/release'
    names = args.harness or HARNESSES
    for name in names:
        if not (build / 'tests' / name).is_file():
            raise RuntimeError('Build android-tests and android-unit-tests before running: ' + name)
    ndk = json.loads((ROOT / 'mobile/toolchain.json').read_text())['android']['ndk']
    prebuilt = next((args.android_sdk / 'ndk' / ndk / 'toolchains/llvm/prebuilt').iterdir())
    remote = '/data/local/tmp/glob2-tests-' + uuid.uuid4().hex
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    summary = {'serial': args.serial, 'arch': args.arch, 'remoteDirectory': remote,
               'mode': 'native Android CPU with SDL dummy video; no JVM or audio device',
               'tests': []}
    with tempfile.TemporaryDirectory(prefix='glob2-device-tests-') as directory:
        payload = Path(directory)
        for name in names:
            subprocess.run([str(prebuilt / 'bin/llvm-strip'), '--strip-debug', '-o',
                            str(payload / name), str(build / 'tests' / name)], check=True)
            (payload / name).chmod(0o755)
        generated = build / 'android-project/app/generated'
        for library in (generated / 'jniLibs' / args.arch).glob('*.so'):
            if library.name != 'libmain.so':
                shutil.copy2(library, payload / library.name)
        for name in ('data', 'maps', 'campaigns', 'scripts'):
            shutil.copytree(generated / 'assets/glob2-bundle' / name, payload / name)
        command('push', str(payload), remote, stdout=subprocess.DEVNULL)
    for name in names:
        profile = remote + '/profiles/' + name
        extra = ['android-session'] if name == 'EngineSessionHarness' else []
        if name == 'TeamStatsSaveHarness':
            extra = ['glob2-save-test-android', remote]
        timeout = 240
        invocation = ['env', 'LD_LIBRARY_PATH=' + remote, 'SDL_VIDEODRIVER=dummy',
                      'SDL_RENDER_DRIVER=software', 'SDL_AUDIODRIVER=dummy',
                      'GLOB2_USER_DATA_DIR=' + profile, 'GLOB2_ASSET_DIR=' + remote,
                      'timeout', str(timeout), './' + name] + extra
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
        digest = hashlib.sha256((build / 'tests' / name).read_bytes()).hexdigest()
        summary['tests'].append({'name': name, 'exitCode': code, 'binarySha256': digest})
        (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(('PASS' if code == 0 else 'FAIL') + ': ' + name, flush=True)
    # Keep remote fixtures for failure diagnosis. Never remove application data.
    if any(test['exitCode'] for test in summary['tests']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
