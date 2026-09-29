#!/usr/bin/env python3
"""Configure and run an isolated Android emulator with a separate ADB server."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
from developer_apk import java_environment

ROOT=Path(__file__).resolve().parents[1]


def properties(path):
    if not path.is_file(): raise ValueError('Missing SDK package: '+str(path.parent))
    return dict(line.split('=',1) for line in path.read_text().splitlines() if '=' in line and not line.startswith('#'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['configure','run','adb-server','status','stop'])
    parser.add_argument('--arch',choices=['arm64-v8a','x86_64'],default='arm64-v8a' if platform.machine() in ('arm64','aarch64') else 'x86_64')
    parser.add_argument('--port',type=int,default=5580)
    parser.add_argument('--adb-port',type=int,default=15037)
    parser.add_argument('--window',action='store_true')
    args=parser.parse_args()
    if args.port%2 or not 5554<=args.port<=5682: raise ValueError('Emulator --port must be even, between 5554 and 5682')
    if not 1024<=args.adb_port<=65535 or args.adb_port in (5037,args.port,args.port+1):
        raise ValueError('Choose a separate unprivileged ADB port, outside the emulator console/transport ports')
    lock=json.loads((ROOT/'mobile/emulator.json').read_text())
    tools=ROOT/'build/mobile-tools';sdk=tools/'android-sdk'
    env=java_environment(ROOT)
    env.pop('ADB_SERVER_SOCKET',None)
    env.pop('ANDROID_SERIAL',None)
    env.update(ANDROID_USER_HOME=str(tools/'android-user'),ANDROID_EMULATOR_HOME=str(tools/'android-user'),
        ANDROID_AVD_HOME=str(tools/'avd'),ANDROID_HOME=str(sdk),ANDROID_SDK_ROOT=str(sdk),ANDROID_ADB_SERVER_PORT=str(args.adb_port),
        ANDROID_EMU_CRASH_REPORTING_DATABASE=str(tools/'emulator-crash.db'),
        ANDROID_EMULATOR_DISCOVERY_DIR=str(tools/'emulator-discovery'),
        ADB_MDNS_AUTO_CONNECT='',ADB_LOCAL_TRANSPORT_MAX_PORT='0',TMPDIR=str(tools/'tmp'))
    for directory in ('android-user','avd','tmp','emulator-discovery'): (tools/directory).mkdir(parents=True,exist_ok=True)
    adb=[str(sdk/'platform-tools/adb'),'-P',str(args.adb_port)]
    if args.command=='adb-server':
        # This server must not attach to the user's USB devices or other emulators.
        os.execvpe(adb[0],adb+['--one-device','glob2-mobile-no-usb','server','nodaemon'],env)
    name='glob2-api'+str(lock['api'])+'-'+('arm64' if args.arch=='arm64-v8a' else 'x64')
    avd=tools/'avd'/(name+'.avd')
    if args.command in ('status','stop'):
        target=adb+['-s','emulator-'+str(args.port)]
        actual=subprocess.check_output(target+['emu','avd','name'],env=env,text=True).splitlines()[0]
        if actual!=name: raise ValueError('Selected emulator belongs to another AVD: '+actual)
        if args.command=='stop':
            subprocess.run(target+['emu','kill'],env=env,check=True)
            return 0
        ready=subprocess.check_output(target+['shell','getprop','sys.boot_completed'],env=env,text=True).strip()
        print(ready or 'booting')
        return 0 if ready=='1' else 1
    image=sdk/'system-images'/('android-'+str(lock['api']))/lock['tag']/args.arch
    if properties(sdk/'emulator/source.properties').get('Pkg.Revision')!=lock['emulator_revision']:
        raise ValueError('Emulator version differs from mobile/emulator.json')
    if properties(image/'source.properties').get('Pkg.Revision')!=lock['image_revision']:
        raise ValueError('System image version differs from mobile/emulator.json')
    if args.command=='configure':
        if (avd/'config.ini').exists():
            print('Existing task-local AVD:',avd);return
        package='system-images;android-'+str(lock['api'])+';'+lock['tag']+';'+args.arch
        subprocess.run([str(sdk/'cmdline-tools/19.0/bin/avdmanager'),'create','avd','--name',name,
            '--package',package,'--path',str(avd)],input='no\n',text=True,env=env,check=True)
        # A small deterministic surface avoids spending hosted CPU rendering a
        # default high-density handset before the startup checks can run.
        config = avd/'config.ini'
        settings = properties(config)
        settings.update({'hw.lcd.width': '320', 'hw.lcd.height': '640',
                         'hw.lcd.density': '160', 'skin.name': '320x640',
                         'hw.keyboard': 'yes'})
        config.write_text(''.join(key+'='+value+'\n' for key,value in settings.items()))
        return
    if not (avd/'config.ini').is_file(): raise ValueError('Run mobile/emulator.py configure first')
    emulator=str(sdk/'emulator/emulator')
    command=[emulator,'-avd',name,'-port',str(args.port),'-no-audio','-no-boot-anim','-no-snapshot',
        '-no-metrics','-crash-report-mode','disabled','-datadir',str(avd),'-data',str(avd/'userdata-qemu.img'),
        '-gpu','swiftshader','-memory','2048','-cores','2']
    if not args.window: command.append('-no-window')
    os.execvpe(emulator,command,env)


if __name__=='__main__':
    try: sys.exit(main())
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);sys.exit(1)
