"""Task-local Android developer signing, with provenance checked before install."""
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def java_environment(root):
    env=dict(os.environ)
    bundled=root/'build/mobile-tools/jdk-17.0.20.1+1/Contents/Home'
    if bundled.is_dir() and 'JAVA_HOME' not in env:
        env['JAVA_HOME']=str(bundled)
    return env


def build_tools(root,sdk):
    lock=json.loads((root/'mobile/toolchain.json').read_text())
    return sdk/'build-tools'/lock['android']['build_tools']


def verify_alignment(root,sdk,apk):
    subprocess.run([str(build_tools(root,sdk)/'zipalign'),'-c','-P','16','4',str(apk)],check=True)


def sign(root,sdk,project):
    from build_layout import BuildLock
    directory=project/'app/build/outputs/apk/release'
    original=directory/'app-release-unsigned.apk'
    if not original.is_file(): raise ValueError('Build the release APK before developer signing')
    verify_alignment(root,sdk,original)
    tools=root/'build/mobile-tools'
    key=tools/'android-user/debug.keystore'
    env=java_environment(root)
    keytool=str(Path(env['JAVA_HOME'])/'bin/keytool') if 'JAVA_HOME' in env else 'keytool'
    with BuildLock(tools):
        key.parent.mkdir(parents=True,exist_ok=True)
        if not key.exists():
            subprocess.run([keytool,'-genkeypair','-keystore',str(key),'-storepass','android',
                '-alias','androiddebugkey','-keypass','android','-keyalg','RSA','-keysize','2048',
                '-validity','10000','-dname','CN=Android Debug,O=Android,C=US'],env=env,check=True)
            key.chmod(0o600)
    apk=directory/'app-release-development.apk'
    temporary=directory/'app-release-development.tmp.apk'
    try:
        subprocess.run([str(build_tools(root,sdk)/'apksigner'),'sign','--ks',str(key),
            '--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android',
            '--v4-signing-enabled','false','--out',str(temporary),str(original)],env=env,check=True)
        subprocess.run([str(build_tools(root,sdk)/'apksigner'),'verify',str(temporary)],env=env,check=True)
        verify_alignment(root,sdk,temporary)
        temporary.replace(apk)
        apk.with_suffix('.json').write_text(json.dumps({'unsigned_sha256':digest(original),
            'signed_sha256':digest(apk)},indent=2)+'\n')
    finally:
        temporary.unlink(missing_ok=True)
    print('Developer-signed APK:',apk)
    return apk


def verified(root,sdk,project):
    directory=project/'app/build/outputs/apk/release'
    apk=directory/'app-release-development.apk'
    original=directory/'app-release-unsigned.apk'
    metadata=apk.with_suffix('.json')
    if not apk.is_file() or not metadata.is_file() or not original.is_file():
        raise ValueError('Run mobile/android.py sign --release for this architecture before installing')
    provenance=json.loads(metadata.read_text())
    if provenance.get('signed_sha256')!=digest(apk) or provenance.get('unsigned_sha256')!=digest(original):
        raise ValueError('Developer APK is stale or changed; sign the current release APK again')
    subprocess.run([str(build_tools(root,sdk)/'apksigner'),'verify',str(apk)],env=java_environment(root),check=True)
    return apk
