#!/usr/bin/env python3
"""Evidence-only read-only dependency snapshot; preserve deltas before failing."""
import argparse,hashlib,json,subprocess
from pathlib import Path

def snapshot(prefix):
    files={str(p.relative_to(prefix)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(prefix.rglob('*')) if p.is_file() and p.name!='.ci-cache.json'}
    brew={}
    for name in 'freetype libpng jpeg-turbo opusfile opus libogg speex openssl@3 boost fribidi libepoxy'.split():
        directory=Path(subprocess.check_output(['brew','--prefix',name],text=True).strip())
        for p in sorted((directory/'lib').rglob('*')):
            if p.is_file() and '.dylib' in p.name:brew[name+'/'+str(p.relative_to(directory))]=hashlib.sha256(p.read_bytes()).hexdigest()
    return {'files':files,'brew_libraries':brew,'recording_manifest':json.loads((prefix/'recording/recording-manifest.json').read_text())}

def delta(a,b):
    return {'added':sorted(b.keys()-a.keys()),'removed':sorted(a.keys()-b.keys()),'changed':sorted(k for k in a.keys()&b.keys() if a[k]!=b[k])}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('prefix',type=Path);p.add_argument('output',type=Path);p.add_argument('--previous',type=Path);p.add_argument('--require-match',action='store_true');p.add_argument('--verify-seal',action='store_true');args=p.parse_args()
    result=snapshot(args.prefix);failed=False
    if args.verify_seal:
        result['seal_delta']=delta(json.loads((args.prefix/'.ci-cache.json').read_text())['files'],result['files'])
        failed=any(result['seal_delta'].values())
    if args.previous:
        previous=json.loads(args.previous.read_text());result['previous']=str(args.previous)
        result['prefix_delta']=delta(previous['files'],result['files']);result['brew_delta']=delta(previous['brew_libraries'],result['brew_libraries'])
        if args.require_match:failed=failed or any(result['prefix_delta'].values()) or any(result['brew_delta'].values())
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k.endswith('_delta')},indent=2))
    if failed:raise SystemExit('Dependency identity changed; exact delta retained in '+str(args.output))
