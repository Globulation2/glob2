#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Controlled persistent-helper lifetime proof, with no OpenCL driver or GPU.

Evidence owner only. The shared resource lock is acquired before compilation.
"""
import argparse
import fcntl
import json
from pathlib import Path
import subprocess

from contracts import HERE, ROOT, sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lock',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    with args.lock.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        check_locked(args)


def check_locked(args):
    subprocess.run(['git','diff','--quiet'],cwd=ROOT,check=True)
    subprocess.run(['git','diff','--cached','--quiet'],cwd=ROOT,check=True)
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    args.output.mkdir(parents=True,exist_ok=False);directory=args.output.resolve()
    paths=(HERE/'persistent_native.cpp',HERE/'persistent_failure_test.cpp',Path(__file__).resolve(),
        HERE/'contracts.py',HERE/'corpus.py',ROOT/'src/field/AdaptiveGradientPolicy.h')
    sources={str(path.relative_to(ROOT)):sha(path) for path in paths}
    executable=directory/'persistent-failure-test'
    command=['g++','-std=c++17','-O2','-DNDEBUG',str(HERE/'persistent_failure_test.cpp'),'-o',str(executable)]
    freeze=dict(schema=1,purpose='controlled native lifetime test; no GPU or production admission',
        revision=revision,sources=sources,command=command,campaign_lock=str(args.lock.resolve()))
    (directory/'freeze.json').write_text(json.dumps(freeze,indent=2))
    compiled=subprocess.run(command,capture_output=True,text=True)
    (directory/'compiler.json').write_text(json.dumps(dict(returncode=compiled.returncode,
        stdout=compiled.stdout,stderr=compiled.stderr),indent=2))
    compiled.check_returncode()
    records=[]
    for scenario in ('drained','fatal'):
        receipt=directory/f'{scenario}-native.json';unwound=directory/f'{scenario}-unwound.txt'
        result=subprocess.run([str(executable),scenario,str(receipt),str(unwound)],capture_output=True,text=True)
        row=dict(scenario=scenario,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr,
            fatal_receipt_exists=receipt.exists(),destructor_sentinel_exists=unwound.exists())
        if receipt.exists():row['fatal_receipt']=json.loads(receipt.read_text())
        records.append(row)
        (directory/'results.json').write_text(json.dumps(records,indent=2))
        if scenario=='drained':
            if result.returncode!=0 or not unwound.exists() or receipt.exists() or 'seeds/output intact' not in result.stdout:
                raise RuntimeError(f'successful drain lifetime proof failed: {row}')
        else:
            expected=dict(schema='glob2-persistent-fatal-drain-v1',api_error=-17,drain_error=-42,
                exit_code=86,completed=False,safe_recovery=False)
            if result.returncode!=86 or unwound.exists() or row.get('fatal_receipt')!=expected or 'PERSISTENT_FATAL_DRAIN' not in result.stderr or 'STUB_FATAL_SENTINELS_INTACT' not in result.stderr:
                raise RuntimeError(f'fatal non-unwinding lifetime proof failed: {row}')
    if sources!={str(path.relative_to(ROOT)):sha(path) for path in paths}:
        raise RuntimeError('sources changed during controlled lifetime proof')
    (directory/'complete.json').write_text(json.dumps(dict(schema=1,exact=True,gpu_loaded=False,
        drained_returncode=0,fatal_returncode=86,fatal_destructor_unwound=False,
        native_binary_sha256=sha(executable),freeze_sha256=sha(directory/'freeze.json'),
        results_sha256=sha(directory/'results.json'),production_admission=False),indent=2))
    print(json.dumps(dict(exact=True,gpu_loaded=False,output=str(directory))))


if __name__=='__main__':main()
