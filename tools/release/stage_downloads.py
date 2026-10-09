#!/usr/bin/env python3
"""Map known build outputs to an explicit package inventory and draft publication.

This mapping belongs to the release producer, never to website filename guessing.
Only the final signed packages are selected. Publication refuses existing tags.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def inventory(directory, tag, commit):
    if not re.fullmatch(r'v[0-9]+(?:\.[0-9]+)+', tag) or not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('Invalid release identity')
    version=tag[1:]; packages=[]
    def add(platform, arch, kind, minimum, filename, dependencies=None):
        path=directory/filename
        if not path.is_file(): raise ValueError('Missing final package: '+filename)
        item=dict(platform=platform,architecture=arch,format=kind,minimumOs=minimum,filename=filename)
        if dependencies: item['dependencies']=dependencies
        packages.append(item)
    def normalize(pattern, filename):
        final=directory/filename
        if final.is_file(): return filename
        matches=list(directory.glob(pattern))
        if len(matches)!=1: raise ValueError('Expected exactly one build output for '+pattern)
        matches[0].rename(final); return filename
    add('windows','x86_64','exe','Windows 10 (64-bit)',f'glob2-{version}-windows-x86_64-setup.exe')
    add('windows','x86_64','zip','Windows 10 (64-bit)',f'glob2-{version}-windows-x86_64.zip')
    for arch in ('arm64','x86_64'): add('macos',arch,'dmg','macOS 15.0',f'Glob2-{version}-macos-{arch}.dmg')
    add('linux','x86_64','flatpak','Flatpak with GNOME 50 runtime','glob2.flatpak')
    snap=normalize('globulation2_*.snap',f'glob2-{version}-linux-x86_64.snap')
    add('linux','x86_64','snap','Linux with snapd',snap)
    rpm=normalize(f'glob2-{version}-*.fc43.x86_64.rpm',f'glob2-{version}-linux-x86_64-fedora43.rpm')
    add('linux','x86_64','rpm','Fedora 43',rpm)
    add('linux','x86_64','tar.gz','Ubuntu 22.04 (64-bit)',f'glob2-{version}-linux-x86_64-ubuntu22.04.tar.gz',
        ['Requires the system libraries listed in the archive INSTALL.txt file.'])
    for abi,arch in [('arm64-v8a','arm64'),('armeabi-v7a','armv7'),('x86_64','x86_64')]:
        add('android',arch,'apk','Android 7.0 (API 24)',f'glob2-{version}-android-{abi}.apk')
    source=f'glob2-{version}.tar.gz'
    if not (directory/source).is_file(): raise ValueError('Missing tagged source archive')
    result=dict(schemaVersion=1,sourceCommit=commit,packages=packages,sources=[dict(filename=source)])
    (directory/'package-inventory.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts',type=Path,required=True)
    parser.add_argument('--tag',required=True); parser.add_argument('--source-commit',required=True)
    parser.add_argument('--list-files',action='store_true'); parser.add_argument('--publish-draft',action='store_true')
    args=parser.parse_args(); data=inventory(args.artifacts,args.tag,args.source_commit)
    files=[str(args.artifacts/item['filename']) for item in data['packages']+data['sources']]
    files.append(str(args.artifacts/'package-inventory.json'))
    if args.list_files: print('\n'.join(files))
    if args.publish_draft:
        subprocess.run(['gh','release','create',args.tag,'--repo','Globulation2/glob2','--verify-tag',
                        '--draft','--generate-notes',*files],check=True)


if __name__=='__main__': main()
