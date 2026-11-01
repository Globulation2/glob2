#!/usr/bin/env python3
"""Verify a Thai APK and select its actual Android app locale before the existing smoke."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile

FILES=('data/texts.th.txt','data/fonts/scripts/NotoSansThai-Regular.ttf',
       'data/fonts/LICENSE-Noto.txt','data/fonts/LICENSE-HarfBuzz.txt')
PACKAGE='org.globulation2.glob2'


def apk_hashes(root, apk):
    hashes={}
    with zipfile.ZipFile(apk) as package:
        for name in FILES:
            expected=hashlib.sha256((root/name).read_bytes()).hexdigest()
            actual=hashlib.sha256(package.read('assets/glob2-bundle/'+name)).hexdigest()
            if actual!=expected:raise ValueError('APK localization asset differs from source: '+name)
            hashes[name]=actual
    hashes['apk']=hashlib.sha256(apk.read_bytes()).hexdigest()
    return hashes


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True);parser.add_argument('--avd',required=True)
    parser.add_argument('--adb-port',type=int,default=15037);parser.add_argument('--locale',default='th')
    args=parser.parse_args()
    if args.locale!='th':raise ValueError('Only the reviewed Thai locale is supported')
    from smoke import validate_target
    from dev_paths import android_sdk
    from asset_bundle import verify_apk_assets
    from developer_apk import verified
    validate_target(args.serial,args.adb_port,args.avd)
    root=Path(__file__).resolve().parents[1];sdk=android_sdk(root)
    project=root/'build/android/device/x86_64/24/client/release/android-project'
    apk=verified(root,sdk,project)
    output=root/'build/mobile-smoke';output.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
    adb=[str(sdk/'platform-tools/adb'),'-P',str(args.adb_port),'-s',args.serial]
    commands=[];result={'locale':'th','passed':False,'commands':commands,'visual_review_required':True,
                       'coverage':'APK Thai assets + Android per-app locale; existing smoke produces runtime evidence separately.',
                       'excluded':['physical device','Thai IME interaction','fleeting Android unpack label visual capture']}
    def run(parts):
        commands.append(parts);return subprocess.check_output(parts,env=env,text=True,stderr=subprocess.STDOUT,timeout=120).strip()
    try:
        if run(adb+['emu','avd','name']).splitlines()[0]!=args.avd:raise ValueError('Wrong emulator AVD')
        if run(adb+['shell','getprop','ro.kernel.qemu'])!='1':raise ValueError('Physical device is not allowed')
        result['revision']=run(['git','rev-parse','HEAD'])
        verify_apk_assets(apk);result['hashes']=apk_hashes(root,apk)
        tools=json.loads((root/'mobile/toolchain.json').read_text())['android']['build_tools']
        resources=run([str(sdk/'build-tools'/tools/'aapt2'),'dump','resources',str(apk)])
        (output/'thai-apk-resources.txt').write_text(resources+'\n')
        target=ET.parse(root/'mobile/android/app/src/main/res/values-th/strings.xml').find("string[@name='preparing_game']").text
        if target not in resources:raise ValueError('Compiled APK does not contain reviewed Thai unpack label')
        result['preparing_game']=target
        run(adb+['shell','am','force-stop',PACKAGE])
        run(adb+['shell','cmd','locale','set-app-locales',PACKAGE,'--user','0','--locales','th-TH'])
        actual=run(adb+['shell','cmd','locale','get-app-locales',PACKAGE,'--user','0'])
        if 'th-TH' not in actual:raise ValueError('Android did not retain the requested Thai app locale')
        result['actual_app_locale']=actual
        result['language_prefilled']=False
        result['passed']=True
    except Exception as exc:
        result['error']=str(exc);raise
    finally:(output/'thai-localization.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')


if __name__=='__main__':main()
