#!/usr/bin/env python3
"""Build mobile dependencies from the pinned vcpkg registry in task-local directories."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scons'))
from build_layout import build_identity, default_directory, prepare_directory, BuildLock, write_if_changed
from mobile_toolchain import discover, ROOT, LOCK
from mobile_artifacts import verify_android_library


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default='android', choices=['android','ios'])
    parser.add_argument('--arch')
    parser.add_argument('--environment',default='device',choices=['device','simulator'])
    parser.add_argument('--developer-dir')
    parser.add_argument('--android-sdk', default=str(ROOT/'build/mobile-tools/android-sdk'))
    parser.add_argument('--release', action='store_true')
    args = parser.parse_args()
    args.arch = args.arch or ('arm64-v8a' if args.target=='android' else 'arm64')
    options = {'target':args.target, 'arch':args.arch, 'environment':args.environment,
               'release':int(args.release), 'android_sdk':args.android_sdk}
    if args.developer_dir: options['developer_dir']=args.developer_dir
    identity=build_identity(options)
    toolchain=discover(identity,options)
    output=ROOT/default_directory(identity)
    output.mkdir(parents=True,exist_ok=True)
    with BuildLock(output):
        prepare_directory(output,identity)
        vcpkg=ROOT/'build/mobile-tools/vcpkg'
        revision=json.loads((ROOT/'mobile/vcpkg.json').read_text())['builtin-baseline']
        with BuildLock(ROOT/'build/mobile-tools'):
            if not (vcpkg/'.git').is_dir():
                subprocess.run(['git','clone','--filter=blob:none','--no-checkout','https://github.com/microsoft/vcpkg.git',str(vcpkg)],check=True)
            current=subprocess.check_output(['git','-C',str(vcpkg),'rev-parse','HEAD'],text=True).strip()
            if current!=revision or not (vcpkg/'bootstrap-vcpkg.sh').exists():
                subprocess.run(['git','-C',str(vcpkg),'checkout','--detach',revision],check=True)
            if not (vcpkg/'vcpkg').exists():
                subprocess.run([str(vcpkg/'bootstrap-vcpkg.sh'),'-disableMetrics'],check=True)
        arch={'arm64-v8a':'arm64','armeabi-v7a':'arm','x86_64':'x64','arm64':'arm64'}[args.arch]
        triplet='glob2-'+arch+'-'+args.target+('-simulator' if args.environment=='simulator' else '')
        installed=output/'vcpkg-installed'
        env=dict(os.environ)
        (output/'tmp').mkdir(exist_ok=True)
        (ROOT/'build/mobile-tools/registries').mkdir(exist_ok=True)
        for name in ('CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','SDKROOT','MACOSX_DEPLOYMENT_TARGET'):
            env.pop(name,None)
        if args.target=='android':
            ndk=json.loads(LOCK.read_text())['android']['ndk']
            env['ANDROID_NDK_HOME']=str(Path(args.android_sdk).resolve()/'ndk'/ndk)
        env.update(VCPKG_DISABLE_METRICS='1',VCPKG_DOWNLOADS=str(ROOT/'build/mobile-tools/downloads'),
                   VCPKG_BINARY_SOURCES='clear',VCPKG_MAX_CONCURRENCY='6',
                   VCPKG_REGISTRIES_CACHE=str(ROOT/'build/mobile-tools/registries'),TMPDIR=str(output/'tmp'))
        if args.developer_dir: env['DEVELOPER_DIR']=args.developer_dir
        subprocess.run([str(vcpkg/'vcpkg'),'install','--triplet='+triplet,
            '--overlay-triplets='+str(ROOT/'mobile/triplets'),
            '--x-manifest-root='+str(ROOT/'mobile'), '--x-install-root='+str(installed),
            '--x-buildtrees-root='+str(output/'vcpkg-buildtrees'),
            '--x-packages-root='+str(output/'vcpkg-packages')],env=env,check=True)
        prefix=installed/triplet
        library_directory=prefix/('lib' if args.release else 'debug/lib')
        for path in library_directory.iterdir():
            if args.target=='android' and path.suffix in ('.a','.so'): verify_android_library(path,args.arch)
        archives={str(path.relative_to(prefix)):hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sorted(library_directory.iterdir()) if path.suffix in ('.a','.so')}
        write_if_changed(prefix/'manifest.json',json.dumps({'identity':identity,'toolchain':toolchain['fingerprint'],
            'registry':revision,'archives':archives},indent=2)+'\n')
        print('Dependency prefix:',prefix)
    return 0

if __name__=='__main__':
    try:
        sys.exit(main())
    except (ValueError, subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr)
        sys.exit(1)
