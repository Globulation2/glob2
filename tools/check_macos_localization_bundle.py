#!/usr/bin/env python3
"""Check the actual ad hoc macOS bundle without release credentials or uploads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import subprocess

ASSETS=('data/texts.th.txt','data/texts.en.txt','data/texts.keys.txt',
        'data/fonts/scripts/NotoSansThai-Regular.ttf','data/fonts/sans.ttf',
        'data/fonts/LICENSE-Noto.txt','data/fonts/LICENSE-HarfBuzz.txt','data/fonts/LICENSE-DejaVu.txt',
        'data/fonts/LICENSE-Droid.txt')

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def bundle_resources(root,bundle):
    resources=bundle/'Contents/Resources';hashes={}
    for name in ASSETS:
        path=resources/name
        if not path.is_file() or sha(path)!=sha(root/'content'/name):
            raise ValueError('Missing or changed bundled asset: '+name)
        hashes[name]=sha(path)
    key='NSLocalNetworkUsageDescription'
    source=plistlib.loads((root/'platforms/macos/Info.plist').read_bytes())
    actual=plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
    if actual.get(key)!=source[key]:raise ValueError('English permission purpose mismatch')
    localized=resources/'th.lproj/InfoPlist.strings'
    if not localized.is_file() or localized.read_bytes()!=(root/'platforms/macos/th.lproj/InfoPlist.strings').read_bytes():
        raise ValueError('Thai permission purpose mismatch or misplaced resource')
    thai=re.search(r'"NSLocalNetworkUsageDescription"\s*=\s*"([^"]+)";',localized.read_text(encoding='utf-8')).group(1)
    hashes['th.lproj/InfoPlist.strings']=sha(localized)
    return {'resource_sha256':hashes,'english_purpose':actual[key],'thai_purpose':thai,
            'executable_sha256':sha(bundle/'Contents/MacOS/glob2')}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--bundle',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=args.root.resolve();bundle=(root/args.bundle).resolve();output=(root/args.output).resolve()
    if platform.system()!='Darwin':raise ValueError('Actual macOS bundle qualification requires macOS')
    if not output.is_relative_to(root/'artifacts'):raise ValueError('Evidence must remain under artifacts')
    output.parent.mkdir(parents=True,exist_ok=True)
    report={'passed':False,'commands':[],'scope':'Actual ad hoc macOS bundle assets, signature seal and packaged CLI startup; no notarization or physical-display claim.'}
    def run(command,cwd=root,env=None):
        result=subprocess.run(list(map(str,command)),cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',timeout=180)
        report['commands'].append({'argv':list(map(str,command)),'cwd':str(cwd),'returncode':result.returncode,'output':result.stdout})
        result.check_returncode();return result.stdout.strip()
    try:
        report['revision']=run(['git','rev-parse','HEAD'])
        if run(['git','status','--porcelain','--untracked-files=normal']):raise ValueError('Package QA requires a committed exact revision')
        report.update(bundle_resources(root,bundle))
        run(['plutil','-lint',bundle/'Contents/Info.plist',bundle/'Contents/Resources/th.lproj/InfoPlist.strings'])
        run(['codesign','--verify','--deep','--strict',bundle])
        run(['codesign','-dv','--verbose=2',bundle])
        env={k:v for k,v in os.environ.items() if not k.startswith('DYLD_')}
        env.update(GLOB2_USER_DATA_DIR=str(output.parent/'profiles/package'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
        report['packaged_cli_version']=run([bundle/'Contents/MacOS/glob2','info','version','--format','json'],cwd=bundle/'Contents/Resources',env=env)
        report['passed']=True
    except Exception as exc:
        report['error']=str(exc);raise
    finally:output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
