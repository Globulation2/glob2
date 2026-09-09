#!/usr/bin/env python3
"""Build and run the separate platform-trust test APK on an isolated emulator."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from developer_apk import build_tools, java_environment, verify_alignment
from smoke import validate_target

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=['arm64-v8a', 'x86_64'], default='arm64-v8a')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--avd', required=True)
    parser.add_argument('--adb-port', type=int, default=15037)
    args = parser.parse_args()
    validate_target(args.serial, args.adb_port, args.avd)
    tools = ROOT/'build/mobile-tools'
    sdk = tools/'android-sdk'
    adb = [str(sdk/'platform-tools/adb'), '-P', str(args.adb_port), '-s', args.serial]
    actual = subprocess.check_output(adb+['emu', 'avd', 'name'], text=True).splitlines()[0]
    if actual != args.avd: raise ValueError('Selected emulator belongs to another AVD')
    output = ROOT/f'build/android/device/{args.arch}/26/client/release'
    project = output/'android-project'
    env = java_environment(ROOT)
    env.update(GRADLE_USER_HOME=str(tools/'gradle-home'), ANDROID_USER_HOME=str(tools/'android-user'), TMPDIR=str(output/'tmp'))
    subprocess.run([sys.executable, str(ROOT/'mobile/android.py'), 'configure', '--release', '--arch', args.arch], env=env, check=True)
    subprocess.run([str(tools/'gradle-8.13/bin/gradle'), '--no-daemon', '--project-dir', str(project), 'assembleReleaseAndroidTest'], env=env, check=True)
    original = project/'app/build/outputs/apk/androidTest/release/app-release-androidTest.apk'
    signed = original.with_name('app-release-androidTest-development.apk')
    key = tools/'android-user/debug.keystore'
    if not key.is_file(): raise ValueError('Sign and install the release development APK before running its test APK')
    signer = str(build_tools(ROOT, sdk)/'apksigner')
    subprocess.run([signer, 'sign', '--ks', str(key), '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android',
                    '--key-pass', 'pass:android', '--v4-signing-enabled', 'false', '--out', str(signed), str(original)], env=env, check=True)
    subprocess.run([signer, 'verify', str(signed)], env=env, check=True)
    verify_alignment(ROOT, sdk, signed)
    subprocess.run(adb+['install', '-r', str(signed)], check=True)
    result = subprocess.check_output(adb+['shell', 'am', 'instrument', '-w',
        'org.globulation.glob2.test/org.globulation.glob2.TrustInstrumentation'], text=True, stderr=subprocess.STDOUT, timeout=90)
    diagnostics = ROOT/'build/mobile-trust-android'
    diagnostics.mkdir(parents=True, exist_ok=True)
    (diagnostics/'instrumentation.log').write_text(result)
    passed = 'glob2Trust=PASS' in result or 'glob2Trust = PASS' in result
    (diagnostics/'result.json').write_text(json.dumps({'serial': args.serial, 'avd': args.avd, 'passed': passed}, indent=2)+'\n')
    if not passed: raise RuntimeError(result)
    print('PASS: Android platform trust accepts a valid public chain and rejects malformed/untrusted certificates')


if __name__ == '__main__': main()
