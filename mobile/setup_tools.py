#!/usr/bin/env python3
"""Install checksum-pinned Gradle and optional JDK in the isolated build tree."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from setup_ndk import digest, ROOT, BuildLock


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator',action='store_true',help='Also install the pinned host emulator and matching API 35 image')
    args=parser.parse_args()
    lock=json.loads((ROOT/'mobile/android-tools.json').read_text())
    tools=ROOT/'build/mobile-tools';tools.mkdir(parents=True,exist_ok=True)
    with BuildLock(tools):
        selected=[lock['gradle']]
        if args.emulator:
            emulators=json.loads((ROOT/'mobile/emulator.json').read_text())['archives']
            host=platform.system()+'-'+platform.machine()
            if host not in emulators: raise ValueError('No pinned emulator for '+host)
            arch='arm64-v8a' if platform.machine() in ('arm64','aarch64') else 'x86_64'
            selected.extend([emulators[host],emulators[arch]])
        sdk=lock.get('sdk-tools-'+platform.system())
        if sdk: selected.append(sdk)
        jdk=lock.get('jdk-'+platform.system()+'-'+platform.machine())
        if jdk: selected.append(jdk)
        else: print('Install JDK 17 for this host and set JAVA_HOME when running Gradle.')
        for artifact in selected:
            destination=tools/artifact['directory']
            if destination.exists(): continue
            downloads=tools/'downloads';downloads.mkdir(exist_ok=True)
            archive=downloads/artifact['url'].rsplit('/',1)[-1]
            algorithm='sha256' if 'sha256' in artifact else 'sha1'
            if not archive.exists() or digest(archive,algorithm)!=artifact[algorithm]:
                with tempfile.NamedTemporaryFile(dir=downloads,delete=False) as output:
                    temporary=Path(output.name)
                    try:
                        with urllib.request.urlopen(artifact['url']) as source: shutil.copyfileobj(source,output)
                        output.close()
                        if digest(temporary,algorithm)!=artifact[algorithm]: raise ValueError('Tool archive checksum mismatch')
                        temporary.replace(archive)
                    finally: temporary.unlink(missing_ok=True)
            with tempfile.TemporaryDirectory(dir=tools) as staging:
                if archive.suffix=='.zip': subprocess.run(['unzip','-q',str(archive),'-d',staging],check=True)
                else:
                    with tarfile.open(archive) as source: source.extractall(staging,filter='data')
                destination.parent.mkdir(parents=True,exist_ok=True)
                (Path(staging)/artifact.get('archive_directory',artifact['directory'])).rename(destination)
            print('Installed',destination)

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);sys.exit(1)
