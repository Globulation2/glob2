#!/usr/bin/env python3
"""Download and verify the pinned NDK into build/mobile-tools, without SDK manager changes."""
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import urllib.request
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scons'))
from build_layout import BuildLock
from mobile_toolchain import ROOT, LOCK


def digest(path, algorithm):
    result=hashlib.new(algorithm)
    with path.open('rb') as source:
        while block:=source.read(1024*1024): result.update(block)
    return result.hexdigest()


def main():
    android=json.loads(LOCK.read_text())['android']
    archive=android['ndk_archives'].get(platform.system())
    if not archive: raise ValueError('Automatic NDK setup supports macOS and Linux. Use android_sdk=PATH for an existing SDK.')
    tools=ROOT/'build/mobile-tools'
    tools.mkdir(parents=True,exist_ok=True)
    with BuildLock(tools):
        destination=tools/'android-sdk/ndk'/android['ndk']
        if destination.exists():
            properties=destination/'source.properties'
            if not properties.exists() or 'Pkg.Revision = '+android['ndk'] not in properties.read_text():
                raise ValueError(f'Existing NDK directory has an unexpected revision: {destination}')
            print(destination);return
        downloads=tools/'downloads';downloads.mkdir(exist_ok=True)
        path=downloads/archive['url'].rsplit('/',1)[-1]
        algorithm='sha256' if 'sha256' in archive else 'sha1'
        if not path.exists() or digest(path,algorithm)!=archive[algorithm]:
            with tempfile.NamedTemporaryFile(dir=downloads,delete=False) as output:
                temporary=Path(output.name)
                try:
                    with urllib.request.urlopen(archive['url']) as source: shutil.copyfileobj(source,output)
                    output.close()
                    if digest(temporary,algorithm)!=archive[algorithm]: raise ValueError('NDK archive checksum mismatch')
                    temporary.replace(path)
                finally:
                    temporary.unlink(missing_ok=True)
        destination.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=destination.parent) as staging:
            subprocess.run(['unzip','-q',str(path),'-d',staging],check=True)
            (Path(staging)/'android-ndk-r28c').rename(destination)
        print(destination)
        print('NDK license files are included in the installed package.')

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);sys.exit(1)
