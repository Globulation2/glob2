"""Cross compiler discovery with no native package-manager or compiler fallback."""
from pathlib import Path
import hashlib
import json
import os
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'mobile/toolchain.json'


def run(command):
    try:
        return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f'Cannot run {command[0]}: {getattr(error, "output", str(error))}') from None


def discover(identity, arguments):
    lock = json.loads(LOCK.read_text())
    if identity['target'] == 'android':
        sdk = Path(arguments.get('android_sdk', os.environ.get('ANDROID_SDK_ROOT', ROOT / 'build/mobile-tools/android-sdk'))).resolve()
        ndk = sdk / 'ndk' / lock['android']['ndk']
        properties = ndk / 'source.properties'
        if not properties.is_file():
            raise ValueError(f'Android NDK {lock["android"]["ndk"]} missing at {ndk}. Install it in {sdk} or pass android_sdk=PATH. See docs/mobile/development.md.')
        revision = next((s.split('=', 1)[1].strip() for s in properties.read_text().splitlines() if s.startswith('Pkg.Revision')), '')
        if revision != lock['android']['ndk']:
            raise ValueError(f'NDK revision mismatch: expected {lock["android"]["ndk"]}, got {revision}')
        host = {'Darwin': 'darwin-x86_64', 'Linux': 'linux-x86_64', 'Windows': 'windows-x86_64'}[platform.system()]
        binaries = ndk / 'toolchains/llvm/prebuilt' / host / 'bin'
        triple = {'arm64-v8a': 'aarch64-linux-android', 'armeabi-v7a': 'armv7a-linux-androideabi', 'x86_64': 'x86_64-linux-android'}[identity['arch']]
        suffix = '.cmd' if platform.system() == 'Windows' else ''
        cc = binaries / (triple + identity['api'] + '-clang' + suffix)
        cxx = binaries / (triple + identity['api'] + '-clang++' + suffix)
        ar = binaries / ('llvm-ar.exe' if platform.system() == 'Windows' else 'llvm-ar')
        flags = ['-fPIC']
        links = ['-Wl,--no-undefined']
        if identity['arch'] != 'armeabi-v7a':
            links += ['-Wl,-z,max-page-size=16384']
        sdk_id = revision
    else:
        if platform.system() != 'Darwin':
            raise ValueError('iOS builds require macOS and full Xcode; Command Line Tools alone have no iOS SDK.')
        developer = arguments.get('developer_dir', os.environ.get('DEVELOPER_DIR'))
        xcrun = ['xcrun'] if not developer else ['/usr/bin/env', 'DEVELOPER_DIR=' + developer, 'xcrun']
        xcode = ['xcodebuild', '-version'] if not developer else ['/usr/bin/env', 'DEVELOPER_DIR=' + developer, 'xcodebuild', '-version']
        version = run(xcode)
        if version.splitlines()[0] != 'Xcode ' + lock['ios']['xcode']:
            raise ValueError(f'Expected Xcode {lock["ios"]["xcode"]}, got {version}. Select it with developer_dir=PATH; do not change the system selection.')
        sdk = 'iphonesimulator' if identity['environment'] == 'simulator' else 'iphoneos'
        cc = run(xcrun + ['--sdk', sdk, '--find', 'clang'])
        cxx = run(xcrun + ['--sdk', sdk, '--find', 'clang++'])
        ar = run(xcrun + ['--sdk', sdk, '--find', 'ar'])
        sysroot = run(xcrun + ['--sdk', sdk, '--show-sdk-path'])
        sdk_id = run(xcrun + ['--sdk', sdk, '--show-sdk-version'])
        triple = 'arm64-apple-ios' + identity['api'] + ('-simulator' if sdk == 'iphonesimulator' else '')
        flags = ['-target', triple, '-isysroot', sysroot]
        links = list(flags)
    compiler = run([str(cxx), '--version'])
    fingerprint = hashlib.sha256(json.dumps({'identity': identity, 'compiler': compiler, 'sdk': sdk_id,
        'lock': hashlib.sha256(LOCK.read_bytes()).hexdigest()}, sort_keys=True).encode()).hexdigest()
    return {'cc': str(cc), 'cxx': str(cxx), 'ar': str(ar), 'cflags': flags,
            'ldflags': links, 'fingerprint': fingerprint, 'compiler': compiler, 'sdk': sdk_id}
