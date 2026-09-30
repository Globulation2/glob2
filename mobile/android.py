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
from mobile_artifacts import verify_android_shared_library, verify_android_symbols, verify_android_archive_symbols
import developer_apk
from asset_bundle import include_asset, restore_gzip_assets, verify_apk_assets


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['configure','build','bundle','sign','sign-bundle','install','launch'])
    parser.add_argument('--arch',default='arm64-v8a',choices=['arm64-v8a','armeabi-v7a','x86_64'])
    parser.add_argument('--release',action='store_true')
    parser.add_argument('--android-sdk',default=str(ROOT/'build/mobile-tools/android-sdk'))
    parser.add_argument('--gradle')
    parser.add_argument('--version-code',type=int,default=1,help='Play version code; increase it for each upload')
    parser.add_argument('--keystore',type=Path,help='Private upload keystore for sign-bundle')
    parser.add_argument('--key-alias',help='Upload key alias for sign-bundle')
    parser.add_argument('--serial',help='Required for install/launch; never select an arbitrary device')
    parser.add_argument('--adb-port',type=int,default=5037,help='ADB server port; use a separate port for isolated emulators')
    args=parser.parse_args()
    if not 1<=args.adb_port<=65535: raise ValueError('--adb-port must be between 1 and 65535')
    if args.version_code < 1: raise ValueError('--version-code must be positive')
    if args.command=='bundle' and not args.release: raise ValueError('Play bundles must be release builds')
    identity=build_identity({'target':'android','arch':args.arch,'release':int(args.release)})
    output=ROOT/default_directory(identity)
    project=output/'android-project'
    sdk=Path(args.android_sdk).resolve()
    if args.command=='sign':
        if not args.release: raise ValueError('Debug builds are signed by Gradle; use --release for developer signing')
        developer_apk.sign(ROOT,sdk,project)
        return
    if args.command=='sign-bundle':
        if not args.release: raise ValueError('Play bundles must be release builds')
        if not args.keystore or not args.key_alias: raise ValueError('sign-bundle needs --keystore and --key-alias')
        passwords=('GLOB2_PLAY_STORE_PASSWORD','GLOB2_PLAY_KEY_PASSWORD')
        if any(not os.environ.get(name) for name in passwords):
            raise ValueError('Set GLOB2_PLAY_STORE_PASSWORD and GLOB2_PLAY_KEY_PASSWORD in the environment')
        bundle=project/'app/build/outputs/bundle/release/app-release.aab'
        metadata=bundle.with_suffix('.json')
        if not bundle.is_file() or not metadata.is_file():
            raise ValueError('Build and verify the release bundle before signing')
        provenance=json.loads(metadata.read_text())
        if provenance.get('sha256') != hashlib.sha256(bundle.read_bytes()).hexdigest():
            raise ValueError('Release bundle changed since verification; rebuild it before signing')
        verify_apk_assets(bundle,'base/assets/glob2-bundle/')
        signed=bundle.with_name('app-release-play.aab')
        signed.unlink(missing_ok=True)
        env=developer_apk.java_environment(ROOT)
        jarsigner=str(Path(env['JAVA_HOME'])/'bin/jarsigner') if 'JAVA_HOME' in env else 'jarsigner'
        temporary=signed.with_suffix('.tmp.aab')
        try:
            subprocess.run([jarsigner,'-keystore',str(args.keystore.resolve()),
                '-storepass:env',passwords[0],'-keypass:env',passwords[1],
                '-signedjar',str(temporary),str(bundle),args.key_alias],env=env,check=True)
            verification=subprocess.check_output([jarsigner,'-verify',str(temporary)],env=env,text=True)
            if 'jar verified.' not in verification: raise ValueError('Signed bundle failed JAR verification')
            verify_apk_assets(temporary,'base/assets/glob2-bundle/')
            temporary.replace(signed)
        finally:
            temporary.unlink(missing_ok=True)
        print('Play upload bundle:',signed)
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
        native_command=[sys.executable,str(ROOT/'mobile/android.py'),'configure','--arch',args.arch,
            '--android-sdk',str(sdk),'--version-code',str(args.version_code)]
        if args.release: native_command.append('--release')
        (project/'glob2-build.json').write_text(json.dumps({'root':str(ROOT),'command':native_command,
            'release':args.release,'version_code':args.version_code},indent=2)+'\n')
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
                if source.is_file() and include_asset(source.relative_to(ROOT).as_posix()):
                    relative=source.relative_to(ROOT);names.append(relative.as_posix())
                    digest.update(relative.as_posix().encode()+b'\0'+hashlib.sha256(source.read_bytes()).digest())
                    target=assets/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        (assets/'index.list').write_text(digest.hexdigest()+'\n'+'\n'.join(names)+'\n')
        (project/'local.properties').write_text('sdk.dir='+str(sdk).replace('\\','\\\\').replace(':','\\:')+'\n')
        print('Android Studio project:',project)
    if args.command in ('build','bundle'):
        android_user=ROOT/'build/mobile-tools/android-user'
        android_user.mkdir(parents=True,exist_ok=True)
        env=developer_apk.java_environment(ROOT)
        env.update(GRADLE_USER_HOME=str(ROOT/'build/mobile-tools/gradle-home'),
                   ANDROID_USER_HOME=str(android_user),TMPDIR=str(output/'tmp'))
        gradle=args.gradle or str(ROOT/'build/mobile-tools/gradle-8.13/bin/gradle')
        task='bundleRelease' if args.command=='bundle' else ('assembleRelease' if args.release else 'assembleDebug')
        bundle=project/'app/build/outputs/bundle/release/app-release.aab'
        if args.command=='bundle': bundle.with_suffix('.json').unlink(missing_ok=True)
        subprocess.run([gradle,'--no-daemon','--project-dir',str(project),task],env=env,check=True)
        readelf = prebuilt / 'bin' / ('llvm-readelf.exe' if os.name == 'nt' else 'llvm-readelf')
        if args.command=='bundle':
            restore_gzip_assets(bundle, assets, 'base/assets/glob2-bundle/')
            verify_apk_assets(bundle, 'base/assets/glob2-bundle/')
            member='base/lib/'+args.arch+'/libmain.so'
            build_id=verify_android_archive_symbols(bundle,output/'lib/libmain.so',member,readelf)
            bundle.with_suffix('.json').write_text(json.dumps({'sha256':hashlib.sha256(bundle.read_bytes()).hexdigest(),
                'version_code':args.version_code,'build_id':build_id,'architecture':args.arch},indent=2)+'\n')
            print('Verified Android symbol build ID:',build_id)
            print('Unsigned Play bundle:',bundle)
            return
        apk=project/('app/build/outputs/apk/release/app-release-unsigned.apk' if args.release else 'app/build/outputs/apk/debug/app-debug.apk')
        if args.release and restore_gzip_assets(apk, assets):
            aligned=apk.with_suffix('.aligned.apk')
            try:
                subprocess.run([str(developer_apk.build_tools(ROOT,sdk)/'zipalign'),
                    '-f','-P','16','4',str(apk),str(aligned)],check=True)
                aligned.replace(apk)
            finally:
                aligned.unlink(missing_ok=True)
        developer_apk.verify_alignment(ROOT,sdk,apk)
        verify_apk_assets(apk)
        build_id = verify_android_symbols(apk, output/'lib/libmain.so', args.arch, readelf)
        apk.with_suffix('.symbols.json').write_text(json.dumps({'build_id':build_id, 'architecture':args.arch}, indent=2)+'\n')
        print('Verified Android symbol build ID:', build_id)

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);sys.exit(1)
