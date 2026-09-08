#!/usr/bin/env python3
"""Build and stage a native Android application using the shared SCons manifest."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scons'))
from build_layout import build_identity, default_directory, BuildLock
from mobile_toolchain import ROOT, LOCK
from mobile_artifacts import verify_android_shared_library
import developer_apk


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['configure','build','sign','install','launch'])
    parser.add_argument('--arch',default='arm64-v8a',choices=['arm64-v8a','armeabi-v7a','x86_64'])
    parser.add_argument('--release',action='store_true')
    parser.add_argument('--android-sdk',default=str(ROOT/'build/mobile-tools/android-sdk'))
    parser.add_argument('--gradle')
    parser.add_argument('--serial',help='Required for install/launch; never select an arbitrary device')
    parser.add_argument('--adb-port',type=int,default=5037,help='ADB server port; use a separate port for isolated emulators')
    args=parser.parse_args()
    if not 1<=args.adb_port<=65535: raise ValueError('--adb-port must be between 1 and 65535')
    identity=build_identity({'target':'android','arch':args.arch,'release':int(args.release)})
    output=ROOT/default_directory(identity)
    project=output/'android-project'
    sdk=Path(args.android_sdk).resolve()
    if args.command=='sign':
        if not args.release: raise ValueError('Debug builds are signed by Gradle; use --release for developer signing')
        developer_apk.sign(ROOT,sdk,project)
        return
    if args.command in ('install','launch'):
        if not args.serial: raise ValueError('--serial is required; inspect devices with adb devices')
        adb=[str(sdk/'platform-tools/adb'),'-P',str(args.adb_port),'-s',args.serial]
        if args.command=='install':
            variant='release' if args.release else 'debug'
            apk=developer_apk.verified(ROOT,sdk,project) if args.release else project/f'app/build/outputs/apk/{variant}/app-{variant}.apk'
            subprocess.run(adb+['install','-r',str(apk)],check=True)
        else:
            subprocess.run(adb+['shell','am','start','-n','org.globulation.glob2/.Glob2Activity'],check=True)
        return
    arch={'arm64-v8a':'arm64','armeabi-v7a':'arm','x86_64':'x64'}[args.arch]
    prefix=output/'vcpkg-installed'/('glob2-'+arch+'-android')
    subprocess.run(['scons','target=android','arch='+args.arch,'release='+str(int(args.release)),
        'android_sdk='+str(sdk),'mobile_deps='+str(prefix),'-j8'],cwd=ROOT,check=True)
    with BuildLock(output):
        # Only refresh the generated source inputs, leaving Gradle build products intact.
        shutil.copytree(ROOT/'mobile/android',project,dirs_exist_ok=True)
        shutil.copy2(LOCK,project/'glob2-toolchain.json')
        native_command=[sys.executable,str(ROOT/'mobile/android.py'),'configure','--arch',args.arch,'--android-sdk',str(sdk)]
        if args.release: native_command.append('--release')
        (project/'glob2-build.json').write_text(json.dumps({'root':str(ROOT),'command':native_command,'release':args.release},indent=2)+'\n')
        generated=project/'app/generated'
        if generated.exists(): shutil.rmtree(generated)
        generated.mkdir(parents=True)
        jni=generated/'jniLibs'/args.arch;jni.mkdir(parents=True)
        shutil.copy2(output/'lib/libmain.so',jni/'libmain.so')
        manifest=json.loads((prefix/'manifest.json').read_text())
        for filename in manifest['archives']:
            if filename.endswith('.so'): shutil.copy2(prefix/filename,jni/Path(filename).name)
        ndk=sdk/'ndk'/json.loads(LOCK.read_text())['android']['ndk']
        prebuilt=next((ndk/'toolchains/llvm/prebuilt').iterdir())
        triple={'arm64-v8a':'aarch64-linux-android','armeabi-v7a':'arm-linux-androideabi','x86_64':'x86_64-linux-android'}[args.arch]
        shutil.copy2(prebuilt/'sysroot/usr/lib'/triple/'libc++_shared.so',jni/'libc++_shared.so')
        for library in jni.glob('*.so'):
            verify_android_shared_library(library,args.arch)
        java=list((output/'vcpkg-buildtrees/sdl2/src').glob('*/android-project/app/src/main/java'))
        if len(java)!=1: raise ValueError('Expected one pinned SDL Java source tree; clean the SDL dependency buildtree and rebuild dependencies')
        shutil.copytree(java[0],generated/'java')
        assets=generated/'assets/glob2-bundle';assets.mkdir(parents=True)
        digest=hashlib.sha256();names=[]
        for directory in ('data','maps','campaigns','scripts'):
            for source in sorted((ROOT/directory).rglob('*')):
                if source.is_file():
                    relative=source.relative_to(ROOT);names.append(relative.as_posix())
                    digest.update(relative.as_posix().encode()+b'\0'+hashlib.sha256(source.read_bytes()).digest())
                    target=assets/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        (assets/'index.list').write_text(digest.hexdigest()+'\n'+'\n'.join(names)+'\n')
        (project/'local.properties').write_text('sdk.dir='+str(sdk).replace('\\','\\\\').replace(':','\\:')+'\n')
        print('Android Studio project:',project)
    if args.command=='build':
        android_user=ROOT/'build/mobile-tools/android-user'
        android_user.mkdir(parents=True,exist_ok=True)
        env=dict(os.environ,GRADLE_USER_HOME=str(ROOT/'build/mobile-tools/gradle-home'),
                 ANDROID_USER_HOME=str(android_user),TMPDIR=str(output/'tmp'))
        bundled_java=ROOT/'build/mobile-tools/jdk-17.0.20.1+1/Contents/Home'
        if bundled_java.is_dir() and 'JAVA_HOME' not in env: env['JAVA_HOME']=str(bundled_java)
        gradle=args.gradle or str(ROOT/'build/mobile-tools/gradle-8.13/bin/gradle')
        subprocess.run([gradle,'--no-daemon','--project-dir',str(project),'assembleRelease' if args.release else 'assembleDebug'],env=env,check=True)
        apk=project/('app/build/outputs/apk/release/app-release-unsigned.apk' if args.release else 'app/build/outputs/apk/debug/app-debug.apk')
        developer_apk.verify_alignment(ROOT,sdk,apk)

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);sys.exit(1)
