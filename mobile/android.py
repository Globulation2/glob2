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
from build_layout import build_identity, default_directory, BuildLock, PACKAGE_VERSION
import official_instance
from mobile_toolchain import ROOT, LOCK, discover
from mobile_artifacts import verify_android_shared_library, verify_android_symbols, verify_android_archive_symbols
import developer_apk
from dev_paths import android_sdk, mobile_tools, gradle_home, dependency_prefix
from asset_bundle import restore_gzip_assets, verify_apk_assets


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['configure','build','bundle','sign','sign-bundle','install','launch'])
    parser.add_argument('--arch',default='arm64-v8a',choices=['arm64-v8a','armeabi-v7a','x86_64'])
    parser.add_argument('--release',action='store_true')
    parser.add_argument('--china',action='store_true',help='Package the China local-play client')
    parser.add_argument('--fdroid',action='store_true',help='Apply F-Droid release codes and unsigned APK checks')
    parser.add_argument('--android-sdk',default=None)
    parser.add_argument('--gradle')
    parser.add_argument('--jobs',type=int,default=8,help='SCons native compiler jobs')
    parser.add_argument('--version-code',type=int,help='Android version code; fixed by the F-Droid release manifest in --fdroid mode')
    parser.add_argument('--amazon-apk',action='store_true',help='Package both ARM ABIs for the Amazon Appstore')
    parser.add_argument('--version-name',help='Android release version name')
    parser.add_argument('--keystore',type=Path,help='Private upload keystore for sign-bundle')
    parser.add_argument('--key-alias',help='Upload key alias for sign-bundle')
    parser.add_argument('--serial',help='Required for install/launch; never select an arbitrary device')
    parser.add_argument('--adb-port',type=int,default=5037,help='ADB server port; use a separate port for isolated emulators')
    args=parser.parse_args()
    package_name='org.globulation2.glob2'
    if args.jobs < 1: raise ValueError('--jobs must be positive')
    if not 1<=args.adb_port<=65535: raise ValueError('--adb-port must be between 1 and 65535')
    if args.fdroid:
        from android_release import PACKAGE as release_package, release_identity, version_code, verify_apk
        if release_package != package_name:
            raise ValueError('F-Droid and Play Android package IDs differ')
        if not args.release or args.china or args.amazon_apk or args.command in ('bundle','sign-bundle'):
            raise ValueError('F-Droid requires the standard release APK')
        expected_code=version_code(args.arch)
        expected_name=release_identity()['versionName']
        if args.version_code is not None and args.version_code != expected_code:
            raise ValueError('F-Droid version code differs from the release manifest')
        if args.version_name is not None and args.version_name != expected_name:
            raise ValueError('F-Droid version name differs from the release manifest')
        args.version_code=expected_code
        args.version_name=expected_name
    else:
        args.version_code=1 if args.version_code is None else args.version_code
    if args.version_code < 1: raise ValueError('--version-code must be positive')
    if args.amazon_apk and (not args.release or args.arch != 'arm64-v8a' or args.command not in ('configure','build')):
        raise ValueError('--amazon-apk requires an arm64-v8a release configure or build')
    version_name=args.version_name or (PACKAGE_VERSION if args.release else '0.9.5-mobile-dev')
    if not version_name or any(ch in version_name for ch in '\r\n'):
        raise ValueError('--version-name must be nonempty and on one line')
    if args.amazon_apk and (args.china or version_name != PACKAGE_VERSION):
        raise ValueError('--amazon-apk requires the standard package and PACKAGE_VERSION')
    if args.command=='bundle' and not args.release: raise ValueError('Play bundles must be release builds')
    identity=build_identity({'target':'android','arch':args.arch,'release':int(args.release),
                             'china':int(args.china),'amazon':int(args.amazon_apk)})
    output=ROOT/default_directory(identity)
    project=output/'android-project'
    sdk=android_sdk(ROOT,args.android_sdk)
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
            subprocess.run(adb+['shell','am','start','-n',package_name+'/org.globulation2.glob2.Glob2Activity'],check=True)
        return
    arches=('arm64-v8a','armeabi-v7a') if args.amazon_apk else (args.arch,)
    base_options={'target':'android','release':int(args.release),'china':int(args.china)}
    outputs={abi:ROOT/default_directory(build_identity(dict(base_options,arch=abi,
             amazon=int(args.amazon_apk)))) for abi in arches}
    dependency_outputs={abi:ROOT/default_directory(build_identity(dict(base_options,arch=abi))) for abi in arches}
    prefixes={abi:dependency_prefix(ROOT, build_identity(dict(base_options,arch=abi)), discover(build_identity(dict(base_options,arch=abi)), {'android_sdk':str(sdk)})['fingerprint']) for abi in arches}
    for abi in arches:
        subprocess.run(['scons','target=android','arch='+abi,'release='+str(int(args.release)),
            'china='+str(int(args.china)),'amazon='+str(int(args.amazon_apk)),
            'android_sdk='+str(sdk),'mobile_deps='+str(prefixes[abi]),'-j'+str(args.jobs)],cwd=ROOT,check=True)
    with BuildLock(output):
        # Only refresh the generated source inputs, leaving Gradle build products intact.
        for source_tree in ('app/src/main/java', 'app/src/androidTest/java'):
            staged = project/source_tree
            if staged.exists(): shutil.rmtree(staged)
        shutil.copytree(ROOT/'mobile/android',project,dirs_exist_ok=True)
        shutil.copy2(LOCK,project/'glob2-toolchain.json')
        native_command=[sys.executable,str(ROOT/'mobile/android.py'),'configure','--arch',args.arch,
            '--android-sdk',str(sdk),'--jobs',str(args.jobs),'--version-code',str(args.version_code)]
        if args.amazon_apk: native_command.append('--amazon-apk')
        if args.version_name: native_command.extend(['--version-name',args.version_name])
        if args.release: native_command.append('--release')
        if args.china: native_command.append('--china')
        if args.fdroid: native_command.append('--fdroid')
        (project/'glob2-build.json').write_text(json.dumps({'root':str(ROOT),'command':native_command,
            'release':args.release,'version_code':args.version_code,
            'version_name':version_name,'arch':args.arch,'package_name':package_name,
            'official_instance_host':official_instance.host(official_instance.origin())},indent=2)+'\n')
        generated=project/'app/generated'
        if generated.exists(): shutil.rmtree(generated)
        generated.mkdir(parents=True)
        ndk=sdk/'ndk'/json.loads(LOCK.read_text())['android']['ndk']
        prebuilt=next((ndk/'toolchains/llvm/prebuilt').iterdir())
        library_sets=[]
        for abi in arches:
            jni=generated/'jniLibs'/abi;jni.mkdir(parents=True)
            shutil.copy2(outputs[abi]/'lib/libmain.so',jni/'libmain.so')
            manifest=json.loads((prefixes[abi]/'manifest.json').read_text())
            for filename in manifest['archives']:
                if filename.endswith('.so'): shutil.copy2(prefixes[abi]/filename,jni/Path(filename).name)
            triple={'arm64-v8a':'aarch64-linux-android','armeabi-v7a':'arm-linux-androideabi','x86_64':'x86_64-linux-android'}[abi]
            shutil.copy2(prebuilt/'sysroot/usr/lib'/triple/'libc++_shared.so',jni/'libc++_shared.so')
            for library in jni.glob('*.so'):
                verify_android_shared_library(library,abi)
            library_sets.append({p.name for p in jni.glob('*.so')})
        if len(library_sets)>1 and library_sets[0]!=library_sets[1]:
            raise ValueError('Amazon APK needs matching native libraries in both ARM ABIs')
        java=prefixes[args.arch]/'share/glob2/sdl-java'
        from dependencies import validate_bundle
        validate_bundle(prefixes[args.arch], build_identity(dict(base_options,arch=args.arch)), discover(build_identity(dict(base_options,arch=args.arch)), {'android_sdk':str(sdk)})['fingerprint'])
        if not java.is_dir(): raise ValueError('Dependency bundle lacks pinned SDL Java sources; rebuild dependencies')
        shutil.copytree(java,generated/'java')
        sys.path.insert(0, str(ROOT))
        from tools.package_assets import export_assets
        exported = output/'runtime-assets'
        export_assets(ROOT, exported, platform='android', optimized=args.release)
        assets=generated/'assets/glob2-bundle'
        if assets.exists(): shutil.rmtree(assets)
        assets.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(exported, assets)
        digest=hashlib.sha256();names=[]
        for source in sorted(assets.rglob('*')):
            if source.is_file():
                relative=source.relative_to(assets);names.append(relative.as_posix())
                digest.update(relative.as_posix().encode()+b'\0'+hashlib.sha256(source.read_bytes()).digest())
        (assets/'index.list').write_text(digest.hexdigest()+'\n'+'\n'.join(names)+'\n')
        (project/'local.properties').write_text('sdk.dir='+str(sdk).replace('\\','\\\\').replace(':','\\:')+'\n')
        print('Android Studio project:',project)
    if args.command in ('build','bundle'):
        android_user=ROOT/'build/mobile-tools/android-user'
        android_user.mkdir(parents=True,exist_ok=True)
        env=developer_apk.java_environment(ROOT)
        env.update(GRADLE_USER_HOME=str(gradle_home(ROOT)),
                   ANDROID_USER_HOME=str(android_user),TMPDIR=str(output/'tmp'))
        gradle=args.gradle or str(mobile_tools(ROOT)/'gradle-8.13/bin/gradle')
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
        if args.fdroid:
            digest=verify_apk(apk,args.arch,sdk)
            apk.with_suffix('.sha256').write_text(digest+'  '+apk.name+'\n')
        build_ids={abi:verify_android_symbols(apk, outputs[abi]/'lib/libmain.so', abi, readelf) for abi in arches}
        if args.amazon_apk:
            apk.with_suffix('.json').write_text(json.dumps({'sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),
                'version_code':args.version_code,'version_name':PACKAGE_VERSION,
                'package':'org.globulation2.glob2','architectures':list(arches),
                'build_ids':build_ids},indent=2)+'\n')
        else:
            apk.with_suffix('.symbols.json').write_text(json.dumps({'build_id':build_ids[args.arch], 'architecture':args.arch}, indent=2)+'\n')
        print('Verified Android symbol build IDs:',build_ids)

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);sys.exit(1)
